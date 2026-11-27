#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "expression.h"
#include "symbol.h"
#include "evaluate.h"

#if EVAL_JUMP_CACHE_ENTRY_COUNT > 0
  typedef struct {
    uint32_t source_index;
    uint32_t target_index;
    uint32_t result_index;
    uint8_t query_kind;
    bool valid;
  } EvalJumpCacheEntry;
#endif

// Record of an allocation owned by the evaluator.
typedef struct {
  void *data;
  size_t size;
  bool marked;
} EvalRuntimeAllocation;

// Temporary evaluated values that must remain reachable while a native call
// or deferred expression is executing. Frames live on the evaluator’s C stack.
typedef struct EvalGCRootFrame {
  // A sequence of GC-protected Datum values (or a single value when `count == 1`)
  const Datum *values;

  // Number of consecutive `Datum` values to inspect, starting at `values`.
  // Use `1` for a single `Datum`, or the array length for multiple values.
  // N-th value (starting from 0) in the sequence is addressed as `values[N]`.
  // Last value in a non-empty sequence is addressed as `values[count - 1]`.
  size_t count;

  // Previous active GC root frame in the evaluator’s stack
  struct EvalGCRootFrame *previous;
} EvalGCRootFrame;

// Preserve an outer activation’s local symbol while the same `(do)` runs
// recursively. The original global symbol entry is restored on return.
typedef struct {
  // A pointer to the original symbol-table entry
  // where the saved symbol state will be restored.
  Symbol *symbol;

  // A shallow copy of the symbol state to restore later.
  // Dynamically allocated data can still be mutated through other references.
  Symbol saved;
} EvalSavedLocalSymbol;

// Active `(do)` scope, including implicit user-defined function scopes.
// Frames live on the evaluator’s C stack.
// Local symbols remain in the global symbol table after cleanup.
typedef struct EvalLocalScopeFrame {
  uint32_t expression_index;

  // Head of the LIFO list of local symbols belonging to this activation.
  // Each symbol points to the next one through `Symbol.next_local`.
  // The list ends when `next_local` is `NULL`.
  Symbol *first_local;

  // Previous scope visible to name resolution.
  //
  // Answers: Which enclosing scopes should be searched when evaluating
  // a variable reference?
  //
  // Function calls use definition-site scope resolution, hiding their
  // caller’s scopes.
  //
  // LIFO stack chain.
  struct EvalLocalScopeFrame *previous;

  // Previous physically active scope, including those hidden from name
  // resolution by user-defined function calls.
  //
  // Answers: Which `(do)` activations are still alive, even if their
  // variables are not currently visible?
  //
  // Used for recursive re-entry and local symbol preservation.
  //
  // The `previous_active` chain always follows actual activation order,
  // whereas `previous` follows scope visibility.
  //
  // LIFO stack chain.
  struct EvalLocalScopeFrame *previous_active;

  // Previous local symbol states belonging to earlier activations of the
  // same `(do)` expression. These snapshots are also roots for manual `(gc)`.
  //
  // A dynamically allocated array containing `saved_local_count` entries.
  // Each entry stores a pointer to the original global symbol-table entry
  // and a shallow copy of its previous state.
  //
  // The array is populated by traversing matching active scope frames,
  // from newest to oldest, and their singly linked lists of local symbols.
  //
  // For example, if one activation registers these locals in order:
  //
  //   (local "a" 10)
  //   (local "b" 20)
  //   (local "c" 30)
  //
  // The scope’s local symbols form a singly linked list through `next_local`:
  //
  //   first_local → c → b → a → NULL
  //
  // The snapshot array is populated by visiting these symbols in that order:
  //
  //   saved_locals[0] → c
  //   saved_locals[1] → b
  //   saved_locals[2] → a
  //
  // The array itself is not a linked list.
  EvalSavedLocalSymbol *saved_locals;
  size_t saved_local_count;
} EvalLocalScopeFrame;

// Evaluator call frame for a user-defined pseudo-function.
// The argument values and the frame itself live on the evaluator’s C stack.
typedef struct EvalFunctionCallFrame {
  uint32_t definition_expression_index;
  uint32_t entry_sequence_depth;
  uint32_t call_depth; // Function-call nesting depth

  const Datum *arguments;
  size_t argument_count;

  // LIFO stack chain.
  //
  // For example:
  //   (fn "a" (b))
  //   (fn "b" (c))
  //   (fn "c" 123)
  //   (a)
  //
  // Will produce:
  //   interpreter->function_call
  //   → call frame c (depth 3)
  //   → call frame b (depth 2)
  //   → call frame a (depth 1)
  //   → NULL
  struct EvalFunctionCallFrame *previous;
} EvalFunctionCallFrame;

// Storage supplied and owned by the host application.
typedef struct {
  Expression *expressions;
  uint32_t expression_capacity;

  // Optional source map. If provided, its capacity must cover all expressions.
  ExpressionSourceLocation *expression_source_locations;
  uint32_t expression_source_location_capacity;

  ExpressionAllocation *expression_allocations;
  uint32_t expression_allocation_capacity;

  Symbol *symbols;
  uint32_t symbol_capacity;

  EvalRuntimeAllocation *runtime_allocations;
  uint32_t runtime_allocation_capacity;

  uint32_t *deferred_expression_indices;
  uint32_t deferred_expression_capacity;
} InterpreterStorage;

// State belonging to an individual interpreter instance.
typedef struct Interpreter {
  // Host-provided tables and their capacities.
  InterpreterStorage storage;

  // Number of used entries in each table.
  uint32_t expression_count;
  uint32_t expression_allocation_count;
  uint32_t symbol_count;
  uint32_t runtime_allocation_count;
  uint32_t deferred_expression_count;

  // Active evaluator stack roots for manually invoked garbage collection.
  EvalGCRootFrame *gc_roots;

  // Most recent symbol definition error.
  symbol_define_error_t symbol_define_error;

  // Current expression evaluation result.
  //
  // When `kind` is either `EVAL_EXIT` or `EVAL_FAIL` the evaluator must stop
  // evaluating the program.
  //
  // When a `NativeFunction` fails it must set `kind` to `EVAL_FAIL` and provide
  // `fail_msg`.
  EvalOutcome eval_outcome;

  // (label) and (goto)
  uint32_t current_native_call_expression_index;
  uint32_t pending_jump_index;
  // Call site of the pending (goto), needed if the destination is unreachable.
  uint32_t pending_jump_expression_index;
  uint32_t resume_target_index;

  // Cache only parsed-expression routing, never resolved symbol names.
  // Each bucket holds two entries; a collision replaces its least recently
  // used entry. No runtime allocations or host-provided buffers are needed.
  #if EVAL_JUMP_CACHE_ENTRY_COUNT > 0
    EvalJumpCacheEntry jump_cache[EVAL_JUMP_CACHE_ENTRY_COUNT];
    uint8_t jump_cache_replace_way[EVAL_JUMP_CACHE_ENTRY_COUNT / 2u];
  #endif

  // Pseudo-local scopes, one frame for each active `(do)`.
  // Initially is `NULL`. When evaluator enters a `(do)` block is sets this
  // field to the scope of that `(do)`. When leaving a `(do)` it sets it to
  // `local_scope->previous`, so to the parent scope (either parent `(do)` or
  // to `NULL` when no more parent `(do)`s left, when reaching top-level scope).
  EvalLocalScopeFrame *local_scope;

  // All currently active `(do)` scopes, including those hidden by a function
  // call’s definition-site name resolution. Used for recursive re-entry.
  // LIFO stack chain head pointer.
  EvalLocalScopeFrame *active_local_scope;

  // Current user-defined function invocation, if any.
  EvalFunctionCallFrame *function_call;

  // (depth) and (leave)
  uint32_t current_sequence_depth;
  uint32_t pending_leave_count;

  // (defer)
  uint32_t executing_deferred_depth;

  bool configured;
} Interpreter;

// Bind host-provided storage to one interpreter instance.
// The interpreter must initially be zero-initialized.
bool configure_interpreter_storage(Interpreter *interpreter, const InterpreterStorage *storage);

static inline bool get_last_evaluation_value(Interpreter *interpreter, Datum *result) {
  if (interpreter->eval_outcome.result.kind == SYM_EMPTY) return false;
  *result = interpreter->eval_outcome.result;
  return true;
}

static inline void report_evaluation_failure(Interpreter *interpreter, char *fail_msg) {
  interpreter->eval_outcome.kind = EVAL_FAIL;
  snprintf(
    interpreter->eval_outcome.as.fail_msg,
    sizeof(interpreter->eval_outcome.as.fail_msg),
    "%s",
    fail_msg
  );
}

static inline void report_evaluation_exit(Interpreter *interpreter, int exit_code, Datum result) {
  interpreter->eval_outcome.kind = EVAL_EXIT;
  interpreter->eval_outcome.as.exit_code = exit_code;
  interpreter->eval_outcome.result = result;
}

static inline uint32_t get_current_native_call_expression_index(const Interpreter *interpreter) {
  return interpreter->current_native_call_expression_index;
}

static inline void request_evaluation_jump(Interpreter *interpreter, uint32_t instruction_index) {
  interpreter->pending_jump_index = instruction_index;
  interpreter->pending_jump_expression_index = interpreter->current_native_call_expression_index;
}

static inline uint32_t get_current_sequence_depth(Interpreter *interpreter) {
  return interpreter->current_sequence_depth;
}

// Number of `(do)` scopes to leave to exit the current function, including
// the implicit function scope. Outside functions there is no return depth.
static inline uint32_t get_current_function_return_depth(const Interpreter *interpreter) {
  return interpreter->function_call == NULL
    ? 0
    : interpreter->current_sequence_depth - interpreter->function_call->entry_sequence_depth;
}
