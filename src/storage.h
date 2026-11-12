#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "expression.h"
#include "symbol.h"
#include "evaluate.h"

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

// Storage supplied and owned by the host application.
typedef struct {
  Expression *expressions;
  uint32_t expression_capacity;

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
