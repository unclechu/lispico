#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "evaluate.h"
#include "symbol-error.h"
#include "stdlib.h"
#include "symbol.h"
#include "wipe-memory.h"
#include "storage.h"
#include "stdlib-variables.h"
#include "stdlib-validation-utils.h"

// Intentionally private (internal evaluator initialization state)
typedef struct {
  Interpreter *interpreter;
  bool is_failed;
  char fail_msg[ERR_MSG_SIZE];
} EvalSettingsInterfaceContext;

static Symbol* define_symbol_interface(
  const struct EvalSettingsInterface *self,
  const char *name,
  SymbolKind kind
) {
  EvalSettingsInterfaceContext *ctx = (EvalSettingsInterfaceContext*) self->context;
  if (ctx->is_failed) return NULL; // Already failed, can’t continue
  Symbol *sym = symbol_define(ctx->interpreter, name, kind);
  if (sym == NULL) {
    extract_error_message_from_symbol_define_error(ctx->interpreter, &ctx->fail_msg, name);
    return NULL;
  } else {
    return sym;
  }
}

static Symbol* symbol_lookup_interface(
  const struct EvalSettingsInterface* self,
  const char *name
) {
  EvalSettingsInterfaceContext *ctx = (EvalSettingsInterfaceContext*) self->context;
  return symbol_lookup(ctx->interpreter, name);
}

static void report_failure_interface(
  const struct EvalSettingsInterface *self,
  const char *err_msg
) {
  EvalSettingsInterfaceContext *ctx = (EvalSettingsInterfaceContext*) self->context;
  ctx->is_failed = true;
  char err_msg_prefix[] = "settings callback failure report: ";
  snprintf(ctx->fail_msg, sizeof(ctx->fail_msg), "%s%s", err_msg_prefix, err_msg);
}

// Check whether an expression provides a resumable path to a label.
// Only `(do)` sequences and the branches of `(if)` can be entered by `(goto)`.
// In particular, an `(if)` condition and ordinary function arguments are not
// execution paths that can be resumed at an arbitrary nested expression.
static bool contains_jump_target_uncached(
  Interpreter *interpreter,
  uint32_t expression_index,
  uint32_t target_index
) {
  if (expression_index == EXPR_NONE || expression_index >= interpreter->expression_count) {
    return false;
  }
  if (expression_index == target_index) return true;

  const Expression *expr = &interpreter->storage.expressions[expression_index];
  if (expr->kind != EXPR_LIST) return false;

  uint32_t fn_index = expr->first_child;
  if (fn_index == EXPR_NONE || fn_index >= interpreter->expression_count) return false;

  const Expression *fn = &interpreter->storage.expressions[fn_index];
  if (fn->kind != EXPR_NAME) return false;

  const ExpressionAllocation *allocation = get_expression_allocation(interpreter, fn->data);
  if (allocation == NULL || allocation->data == NULL) return false;

  // Look into (do)-sequences
  if (strcmp(allocation->data, "do") == 0) {
    uint32_t index = fn->next;
    while (index != EXPR_NONE) {
      if (index >= interpreter->expression_count) return false;
      if (contains_jump_target_uncached(interpreter, index, target_index)) return true;
      index = interpreter->storage.expressions[index].next;
    }
  }
  // Look into (if)-branches
  else if (strcmp(allocation->data, "if") == 0) {
    // Only the true and false branches are jump destinations, not the condition.
    uint32_t condition_index = fn->next;
    if (condition_index == EXPR_NONE || condition_index >= interpreter->expression_count) {
      return false;
    }
    uint32_t true_index = interpreter->storage.expressions[condition_index].next;
    if (true_index == EXPR_NONE || true_index >= interpreter->expression_count) return false;
    uint32_t false_index = interpreter->storage.expressions[true_index].next;
    return (
      contains_jump_target_uncached(interpreter, true_index, target_index) ||
      contains_jump_target_uncached(interpreter, false_index, target_index)
    );
  }

  return false;
}

// Find the expression to start executing in a sequence to reach a jump target.
// For nested targets, return the enclosing `(do)` or `(if)` expression.
static uint32_t find_sequence_jump_entry_uncached(
  Interpreter *interpreter,
  uint32_t first_expression_index,
  uint32_t target_index
) {
  uint32_t index = first_expression_index;

  while (index != EXPR_NONE) {
    if (index >= interpreter->expression_count) return EXPR_NONE;
    if (contains_jump_target_uncached(interpreter, index, target_index)) return index;
    index = interpreter->storage.expressions[index].next;
  }

  return EXPR_NONE;
}

// A lookup never changes jump semantics: misses run the original tree walk.
// Separate query kinds prevent a subtree-containment result from being
// mistaken for an entry expression index (including `EXPR_NONE` misses).
enum {
  JUMP_ROUTE_SEQUENCE = 1,
  JUMP_ROUTE_CONTAINS = 2,
};

static uint32_t jump_route_lookup(
  Interpreter *interpreter,
  uint32_t source_index,
  uint32_t target_index,
  uint8_t query_kind
) {
  // Cache lookup and replacement-way update (on cache hit).
  //
  // Look for an already cached jump route in its two-entry bucket.
  // On a hit, return the cached result and mark the other entry as the
  // preferred replacement candidate. On a miss, fall through to the
  // original jump-resolution algorithm.
  #if EVAL_JUMP_CACHE_ENTRY_COUNT > 0
    // A route is identified by its source expression index, target expression
    // index, and query kind. Mix all three fields so nearby indices and
    // different query kinds are less likely to compete for the same bucket.
    //
    // The odd multipliers are established 32-bit integer-hashing constants:
    //   0x9e3779b1: a golden-ratio-derived multiplier used in Knuth-style
    //               multiplicative hashing (decimal 2654435761).
    //   0x85ebca6b: the first multiplier from the MurmurHash3 32-bit finalizer.
    //   0xc2b2ae35: the second multiplier from that same finalizer.
    // We use them as three distinct mixing constants, not as the complete
    // MurmurHash3 algorithm. Unsigned 32-bit multiplication wraps modulo 2^32.
    uint32_t hash = source_index * 0x9e3779b1u;
    hash ^= target_index * 0x85ebca6bu;
    hash ^= (uint32_t) query_kind * 0xc2b2ae35u;

    // Fold the upper 16 hash bits into the lower 16 using XOR. Bucket
    // selection below uses low bits only, so this lets high-bit differences
    // influence the selected bucket too. This is a lightweight mixing step,
    // not a cryptographic hash or a full avalanche finalizer.
    hash ^= hash >> 16;

    // Each bucket holds two cache entries (two ways). With 32 total entries,
    // there are 16 buckets. The configured entry count is a power of two,
    // so the bucket count is also a power of two: masking with (count - 1)
    // selects a bucket just like modulo, without an integer division.
    uint32_t bucket = hash & ((EVAL_JUMP_CACHE_ENTRY_COUNT / 2u) - 1u);

    // Buckets occupy consecutive pairs in the flat cache array. The first
    // slot of bucket B is at index 2*B; the other is at index 2*B + 1.
    uint32_t first = bucket * 2u;

    // Each bucket contains two entries (ways). Check both to see whether
    // either already holds the route we are looking for.
    for (uint32_t way = 0; way < 2; ++way) {
      const EvalJumpCacheEntry *entry = &interpreter->jump_cache[first + way];

      // A cache hit requires a valid entry whose entire key matches:
      // the source expression index, target expression index, and query kind.
      // Checking all three prevents reusing a result from a different route
      // or from a different type of jump-resolution query.
      if (
        entry->valid &&
        entry->source_index == source_index &&
        entry->target_index == target_index &&
        entry->query_kind == query_kind
      ) {
        // This way was just used, so prefer replacing the other way next
        // time a new route needs to occupy this bucket.
        //
        // Since way is either 0 or 1, (1u - way) selects the opposite way:
        //   way 0 was used → replace way 1 next
        //   way 1 was used → replace way 0 next
        //
        // This implements a simple two-way least-recently-used (LRU) policy.
        // On a cache hit, the used entry becomes the most recently used,
        // protecting it from the next replacement within this bucket.
        interpreter->jump_cache_replace_way[bucket] = (uint8_t) (1u - way);

        // Return the cached result without traversing the expression tree.
        // EXPR_NONE is also a valid cached result for an unreachable target,
        // so returning it does not necessarily mean the lookup missed.
        return entry->result_index;
      }
    }
  #endif

  uint32_t result =
    query_kind == JUMP_ROUTE_SEQUENCE
      ? find_sequence_jump_entry_uncached(interpreter, source_index, target_index)
      : (uint32_t) contains_jump_target_uncached(interpreter, source_index, target_index);

  // Cache the uncached result.
  //
  // Store the computed route in the cache and update the next replacement way.
  #if EVAL_JUMP_CACHE_ENTRY_COUNT > 0
    uint32_t way = interpreter->jump_cache_replace_way[bucket];
    if (!interpreter->jump_cache[first].valid) {
      way = 0;
    } else if (!interpreter->jump_cache[first + 1u].valid) {
      way = 1;
    }

    interpreter->jump_cache[first + way] = (EvalJumpCacheEntry) {
      .source_index = source_index,
      .target_index = target_index,
      .result_index = result,
      .query_kind = query_kind,
      .valid = true,
    };

    interpreter->jump_cache_replace_way[bucket] = (uint8_t) (1u - way);
  #endif

  return result;
}

static uint32_t find_sequence_jump_entry(
  Interpreter *interpreter,
  uint32_t first_expression_index,
  uint32_t target_index
) {
  return jump_route_lookup(
    interpreter,
    first_expression_index,
    target_index,
    JUMP_ROUTE_SEQUENCE
  );
}

static bool contains_jump_target(
  Interpreter *interpreter,
  uint32_t expression_index,
  uint32_t target_index
) {
  return jump_route_lookup(
    interpreter,
    expression_index,
    target_index,
    JUMP_ROUTE_CONTAINS
  ) != 0;
}

static bool evaluate_expression(
  Interpreter *interpreter,
  uint32_t current_expression_index,
  Datum *result
);

static bool evaluate_expression_impl(
  Interpreter *interpreter,
  uint32_t current_expression_index,
  Datum *result
);

// Register a GC root frame in the current C scope.
// The frame must remain alive until it is explicitly removed.
// `root_name` declares a local GC root frame that remains alive in this C
// scope. The frame makes `root_values` accessible to the collector and protects
// their referenced runtime allocations until the frame is popped.
#define PUSH_GC_ROOT(interpreter, root_name, root_values, root_count) \
  EvalGCRootFrame root_name = { \
    .values = (root_values), \
    .count = (root_count), \
    .previous = (interpreter)->gc_roots, \
  }; \
  (interpreter)->gc_roots = &(root_name)

// Release an EvalGCRootFrame from GC protection.
// The frame must be the current top of the GC root stack (LIFO order).
#define POP_GC_ROOT(interpreter, root_name) { \
  assert((interpreter)->gc_roots == &(root_name)); \
  (interpreter)->gc_roots = (root_name).previous; \
  wipe_memory(&(root_name), sizeof(root_name)); \
}

// Drop a `(do)` activation’s local references after its deferred expressions.
// Keep symbol-table entries so the same names can be reused on later entries.
static void clean_local_scope(EvalLocalScopeFrame *scope) {
  Symbol *local = scope->first_local;
  while (local != NULL) {
    Symbol *next = local->next_local;
    // Drop every stale value/pointer byte, without touching its allocation.
    // Other live references may still need that data until a later `(gc)`.
    wipe_memory(&local->as, sizeof(local->as));
    local->kind = SYM_VARIABLE;
    local->as.variable = (Value) { .type = TYPE_VOID };
    local->frozen = false;
    local->local_scope_owner = NULL;
    local->next_local = NULL;
    local = next;
  }
}

// Enter a `(do)` activation. If the same expression is already active,
// temporarily preserve its locals so recursive calls receive independent
// values while keeping the existing global symbol table and generated names.
static bool enter_local_scope(Interpreter *interpreter, EvalLocalScopeFrame *scope) {
  size_t count = 0;

  // The nearest previous activation may not have declared a particular
  // local yet. Preserve symbols from every active activation of this same
  // expression, not only from the nearest matching frame.
  for (
    EvalLocalScopeFrame *previous = interpreter->active_local_scope;
    previous != NULL;
    previous = previous->previous_active
  ) {
    // If it’s a different scope preservation is not needed.
    if (previous->expression_index != scope->expression_index) continue;

    // Calculate the `count`.
    for (
      Symbol *local = previous->first_local;
      local != NULL && local->local_scope_owner == previous;
      local = local->next_local
    ) {
      count++;
    }
  }

  // If there’s something to preserve in the local scope.
  if (count != 0) {
    if (count > SIZE_MAX / sizeof(EvalSavedLocalSymbol)) {
      report_evaluation_failure(interpreter, "(do) local activation is too large");
      return false;
    }

    // Allocated saved local scope array.
    EvalSavedLocalSymbol *saved = malloc(count * sizeof(*saved));
    if (saved == NULL) {
      report_evaluation_failure(interpreter, "(do) cannot save recursive local variables");
      return false;
    }

    scope->saved_locals = saved;
    scope->saved_local_count = count;

    // Snapshot every prior activation before clearing any `next_local`
    // pointers, so that earlier lists remain traversable until restored.
    size_t i = 0;
    for (
      EvalLocalScopeFrame *previous = interpreter->active_local_scope;
      previous != NULL;
      previous = previous->previous_active
    ) {
      // If it’s a different scope preservation is not needed.
      if (previous->expression_index != scope->expression_index) continue;

      // Copy the local scope symbols.
      for (
        Symbol *local = previous->first_local;
        local != NULL && local->local_scope_owner == previous;
        local = local->next_local
      ) {
        saved[i].symbol = local; // Pointer of the symbols table entry, to restore to later.
        memcpy(&saved[i].saved, local, sizeof(*local)); // Make a shallow symbol copy.
        i++;
      }
    }

    // Drop these references from the table without freeing their data.
    // The snapshots themselves remain reachable by manual `(gc)`.
    for (size_t i = 0; i < count; ++i) {
      Symbol *symbol = saved[i].symbol;
      wipe_memory(&symbol->as, sizeof(symbol->as));
      symbol->kind = SYM_VARIABLE;
      symbol->as.variable = (Value) { .type = TYPE_VOID };
      symbol->frozen = false;
      symbol->local_scope_owner = NULL;
      symbol->next_local = NULL;
    }
  }

  scope->previous_active = interpreter->active_local_scope;
  interpreter->active_local_scope = scope;
  interpreter->local_scope = scope;
  return true;
}

// Drop this activation’s locals, then restore the suspended outer values.
// Only the current frame is wiped: callers remain live until they return.
static void leave_local_scope(
  Interpreter *interpreter,
  EvalLocalScopeFrame *scope,
  EvalLocalScopeFrame *previous_visible_scope
) {
  assert(interpreter->active_local_scope == scope);
  clean_local_scope(scope);

  for (size_t i = 0; i < scope->saved_local_count; ++i) {
    EvalSavedLocalSymbol *saved = &scope->saved_locals[i];
    // Override symbols table entry to the preserved state.
    *saved->symbol = saved->saved;
  }

  if (scope->saved_locals != NULL) {
    // Wipe the preserved shallow local scope copy and free the memory.
    wipe_memory(scope->saved_locals, scope->saved_local_count * sizeof(*scope->saved_locals));
    free(scope->saved_locals);
  }

  interpreter->active_local_scope = scope->previous_active;
  interpreter->local_scope = previous_visible_scope;
  wipe_memory(scope, sizeof(*scope)); // `scope` is reset after this function call.
}

// Execute deferred expressions in reverse registration order.
// Preserve the original evaluation result and pending control flow.
static bool run_deferred_expressions(Interpreter *interpreter, uint32_t defer_start) {
  // No (defer)s to handle, done
  if (interpreter->deferred_expression_count == defer_start) return true;

  // Preserve current sequence state first
  EvalOutcome saved_outcome = interpreter->eval_outcome;
  Datum saved_result = interpreter->eval_outcome.result;
  // Deferred expressions may invoke (gc) while the original evaluation
  // result is temporarily removed from `eval_outcome`.
  PUSH_GC_ROOT(interpreter, saved_eval_outcome_result, &saved_result, 1);
  uint32_t saved_jump_index = interpreter->pending_jump_index;
  uint32_t saved_jump_expression_index = interpreter->pending_jump_expression_index;
  uint32_t saved_resume_index = interpreter->resume_target_index;
  uint32_t saved_leave_count = interpreter->pending_leave_count;
  uint32_t saved_deferred_depth = interpreter->executing_deferred_depth;

  bool deferred_failure = false;

  interpreter->executing_deferred_depth = interpreter->current_sequence_depth;

  // Evaluate all (defer)s sequentially in reverse order (LIFO)
  while (interpreter->deferred_expression_count > defer_start) {
    interpreter->deferred_expression_count--; // Shift to last (defer) index

    uint32_t expression_index =
      interpreter->storage.deferred_expression_indices[interpreter->deferred_expression_count];
    wipe_memory(
      &interpreter->storage.deferred_expression_indices[interpreter->deferred_expression_count],
      sizeof(*interpreter->storage.deferred_expression_indices)
    );

    // Execute each deferred expression independently of the control flow
    // that caused this sequence to terminate. Each (defer) sees its original
    // sequence state (sort of, basically just evaluation result, (goto) and
    // (leave) jumps are reset). Other (defer)s do not affect state of the others.
    // N.B. Not considering variables (they can be mutated, and different
    // (defer)s will observe these mutations).
    interpreter->eval_outcome = saved_outcome;
    interpreter->eval_outcome.kind = EVAL_OK;
    interpreter->eval_outcome.result = saved_result;
    interpreter->eval_outcome.failure_expression_index = EXPR_NONE;
    interpreter->pending_jump_index = EXPR_NONE;
    interpreter->pending_jump_expression_index = EXPR_NONE;
    interpreter->resume_target_index = EXPR_NONE;
    interpreter->pending_leave_count = 0;

    // Evaluate the (defer) expression
    Datum ignored_result = { .kind = SYM_EMPTY };
    bool ok = evaluate_expression(interpreter, expression_index, &ignored_result);
    wipe_memory(&ignored_result, sizeof(ignored_result));

    // Preserve only the first (defer) failure, but continue executing all defers
    if (
      (
        !ok ||
        interpreter->eval_outcome.kind != EVAL_OK ||
        // (defer)s cannot make jumps outside of their parent sequence scope
        (
          interpreter->pending_jump_index != EXPR_NONE ||
          interpreter->resume_target_index != EXPR_NONE
        ) ||
        // (defer)s cannot invoke (leave)s outside of their parent sequence
        interpreter->pending_leave_count != 0
      ) && !deferred_failure
    ) {
      deferred_failure = true;

      // Preserve an existing failure. Otherwise the first failure from
      // deferred execution replaces the original control-flow outcome.
      // A (defer)’s failure does not override their parent sequence failure.
      // But first (defer)’s failure propagates to the evaluator (causing
      // evaluation failure) when the sequence finishes successfully.
      // When (defer) fails, other (defer)s (including upper in the scope) are
      // still evaluated, though first failure becomes the evaluation failure,
      // while next (defer) failures are just ignored.
      if (saved_outcome.kind != EVAL_FAIL) {
        if (interpreter->eval_outcome.kind == EVAL_FAIL) {
          saved_outcome = interpreter->eval_outcome;
        } else {
          saved_outcome.kind = EVAL_FAIL;
          saved_outcome.failure_expression_index = expression_index;
          snprintf(
            saved_outcome.as.fail_msg,
            sizeof(saved_outcome.as.fail_msg),
            "(defer) deferred expression cannot interrupt its enclosing sequence"
          );
        }

        // Override the result with failure
        saved_outcome.result = saved_result;
      }
    }
  }

  // Restore the original state (except propagate first (defer) failure if there
  // were any, while preserving original sequence failure if there was one which
  // takes priority over (defer)’s failure)
  interpreter->executing_deferred_depth = saved_deferred_depth;
  interpreter->eval_outcome = saved_outcome;
  interpreter->pending_jump_index = saved_jump_index;
  interpreter->pending_jump_expression_index = saved_jump_expression_index;
  interpreter->resume_target_index = saved_resume_index;
  interpreter->pending_leave_count = saved_leave_count;

  if (deferred_failure) {
    // A deferred failure cancels pending jumps, leaves, or exits.
    // The evaluation failure is already stored in `eval_outcome`.
    interpreter->pending_jump_index = EXPR_NONE;
    interpreter->pending_jump_expression_index = EXPR_NONE;
    interpreter->resume_target_index = EXPR_NONE;
    interpreter->pending_leave_count = 0;
  }

  POP_GC_ROOT(interpreter, saved_eval_outcome_result);
  wipe_memory(&saved_result, sizeof(saved_result));
  wipe_memory(&saved_outcome, sizeof(saved_outcome));
  return !deferred_failure;
}

// Evaluate top-level or `(do)` expressions sequence.
// Also supports jumping into inactive nested `(do)` sequences and `(if)` branches.
static bool evaluate_sequence(
  Interpreter *interpreter,
  uint32_t first_expression_index,
  Datum *result,
  bool is_top_level
) {
  uint32_t current_expression_index = first_expression_index;
  uint32_t defer_start = interpreter->deferred_expression_count;
  bool ok = true;

  // A sequence keeps its last result while evaluating later expressions.
  // Nested calls may replace eval_outcome.result before that value is used.
  PUSH_GC_ROOT(interpreter, sequence_gc_roots, result, 1);

  *result = (Datum) { .kind = SYM_EMPTY };

  // If re-entering a nested sequence, skip expressions preceding the target.
  if (interpreter->resume_target_index != EXPR_NONE) {
    current_expression_index = find_sequence_jump_entry(
      interpreter,
      first_expression_index,
      interpreter->resume_target_index
    );

    if (current_expression_index == EXPR_NONE) {
      report_evaluation_failure(interpreter, "(goto) cannot find nested jump destination");
      interpreter->eval_outcome.failure_expression_index =
        interpreter->pending_jump_expression_index;
      ok = false;
      goto finish;
    }
  }

  while (current_expression_index != EXPR_NONE) {
    if (current_expression_index >= interpreter->expression_count) {
      report_evaluation_failure(interpreter, "Invalid expression index");
      interpreter->eval_outcome.failure_expression_index = current_expression_index;
      ok = false;
      goto finish;
    }

    // The requested label has been reached. Resume ordinary evaluation.
    if (current_expression_index == interpreter->resume_target_index) {
      interpreter->resume_target_index = EXPR_NONE;
    }

    if (!evaluate_expression(interpreter, current_expression_index, result)) {
      // Propagate `(leave)` through the requested number of `(do)` sequences.
      if (interpreter->pending_leave_count != 0) {
        if (!is_top_level) {
          interpreter->pending_leave_count--;

          if (interpreter->pending_leave_count == 0) {
            // A leave that finishes at a loop’s implicit iteration scope
            // terminates that loop instead of starting another iteration.
            if (
              interpreter->loop_frame != NULL &&
              interpreter->loop_frame->sequence_depth == interpreter->current_sequence_depth &&
              interpreter->loop_frame->function_call == interpreter->function_call
            ) {
              interpreter->loop_frame->exit_requested = true;
            }
            *result = interpreter->eval_outcome.result;
            goto finish;
          }
        }

        ok = false;
        goto finish;
      }

      if (
        interpreter->eval_outcome.kind == EVAL_OK &&
        interpreter->pending_jump_index != EXPR_NONE
      ) {
        // Check whether this sequence can handle the requested jump,
        // either directly or through nested (do) and (if) expressions.
        uint32_t jump_entry = find_sequence_jump_entry(
          interpreter,
          first_expression_index,
          interpreter->pending_jump_index
        );

        if (jump_entry != EXPR_NONE) {
          // A direct jump needs no nested re-entry.
          // Otherwise remember the target while entering nested (do)s/(if)s.
          interpreter->resume_target_index =
            jump_entry == interpreter->pending_jump_index
              ? EXPR_NONE
              : interpreter->pending_jump_index;

          interpreter->pending_jump_index = EXPR_NONE;
          interpreter->pending_jump_expression_index = EXPR_NONE;
          current_expression_index = jump_entry;
          continue;
        }

        // Otherwise let the enclosing sequence try to handle the jump.
        // Only the top-level sequence reports an unresolved destination.
        if (is_top_level) {
          uint32_t jump_expression_index = interpreter->pending_jump_expression_index;
          interpreter->pending_jump_index = EXPR_NONE;
          interpreter->pending_jump_expression_index = EXPR_NONE;
          report_evaluation_failure(
            interpreter,
            "(goto) destination is not reachable through (do)"
          );
          interpreter->eval_outcome.failure_expression_index = jump_expression_index;
        }
      }

      // Propagate a jump, failure, or exit to the enclosing sequence.
      ok = false;
      goto finish;
    }

    // Save the result of the most recently completed expression.
    interpreter->eval_outcome.result = *result;

    // Move to the next expression in the sequence.
    current_expression_index = interpreter->storage.expressions[current_expression_index].next;
  }

  finish:
    // Deferred expressions run before the enclosing `(do)` decreases
    // `current_sequence_depth`.
    bool deferred_ok = run_deferred_expressions(interpreter, defer_start);
    POP_GC_ROOT(interpreter, sequence_gc_roots);
    if (!deferred_ok) return false;
    return ok;
}

// Record the innermost failing expression before the failure propagates.
// Recursive calls use this wrapper, so outer expressions cannot overwrite it.
static bool evaluate_expression(
  Interpreter *interpreter,
  uint32_t current_expression_index,
  Datum *result
) {
  bool ok = evaluate_expression_impl(
    interpreter,
    current_expression_index,
    result
  );

  if (
    !ok &&
    interpreter->eval_outcome.kind == EVAL_FAIL &&
    interpreter->eval_outcome.failure_expression_index == EXPR_NONE
  ) {
    interpreter->eval_outcome.failure_expression_index = current_expression_index;
  }

  return ok;
}

// Identify the existing expression nodes of a function definition.
// No new expressions are created for the implicit function-body `(do)`.
typedef struct {
  uint32_t name_index;
  uint32_t body_index;
  uint32_t first_argument_index;
  size_t named_argument_count;
  bool variadic;
  bool immutable_arguments;
} UserFunctionDefinition;

static bool get_user_function_definition(
  Interpreter *interpreter,
  uint32_t definition_index,
  UserFunctionDefinition *definition
) {
  if (definition_index >= interpreter->expression_count) goto invalid_definition;

  const Expression *expr = &interpreter->storage.expressions[definition_index];
  if (expr->kind != EXPR_LIST) goto invalid_definition;

  // Function definition expression index.
  uint32_t fn_index = expr->first_child;
  if (fn_index == EXPR_NONE || fn_index >= interpreter->expression_count) goto invalid_definition;

  const Expression *fn = &interpreter->storage.expressions[fn_index];
  if (fn->kind != EXPR_NAME) goto invalid_definition;

  const ExpressionAllocation *fn_name = get_expression_allocation(interpreter, fn->data);
  if (fn_name == NULL || fn_name->data == NULL || strcmp(fn_name->data, "fn") != 0) {
    goto invalid_definition;
  }

  // Get the function name expression index.
  uint32_t name_index = fn->next;
  if (name_index == EXPR_NONE || name_index >= interpreter->expression_count) {
    report_evaluation_failure(interpreter, "(fn) expects a name and a body");
    return false;
  }

  // Get the function body expression index
  // (could be arguments, will be updated later in that case).
  uint32_t body_index = interpreter->storage.expressions[name_index].next;
  if (body_index == EXPR_NONE || body_index >= interpreter->expression_count) {
    report_evaluation_failure(interpreter, "(fn) expects a name and a body");
    return false;
  }

  *definition = (UserFunctionDefinition) {
    .name_index = name_index,
    .body_index = body_index,
    .first_argument_index = EXPR_NONE,
  };

  // Check if second `(fn)` argument is arguments definition, not a function body.
  // Shift `body_index` to third argument in this case.
  if (interpreter->storage.expressions[body_index].next != EXPR_NONE) {
    const Expression *possible_declarations = &interpreter->storage.expressions[body_index];
    if (possible_declarations->kind != EXPR_LIST) goto incorrect_arguments_declaration;

    uint32_t declaration_name_index = possible_declarations->first_child;

    if (declaration_name_index == EXPR_NONE) goto incorrect_arguments_declaration;

    if (declaration_name_index >= interpreter->expression_count) {
      goto incorrect_arguments_declaration;
    }

    const Expression *declaration_name =
      &interpreter->storage.expressions[declaration_name_index];

    if (declaration_name->kind != EXPR_NAME) goto incorrect_arguments_declaration;

    const ExpressionAllocation *name =
      get_expression_allocation(interpreter, declaration_name->data);

    if (name == NULL || name->data == NULL) goto incorrect_arguments_declaration;

    if (
      strcmp(name->data, "args") == 0 ||
      strcmp(name->data, "var-args") == 0 ||
      strcmp(name->data, "immutable-args") == 0 ||
      strcmp(name->data, "immutable-var-args") == 0
    ) {
      definition->variadic = (
        strcmp(name->data, "var-args") == 0 ||
        strcmp(name->data, "immutable-var-args") == 0
      );
      definition->immutable_arguments = (
        strcmp(name->data, "immutable-args") == 0 ||
        strcmp(name->data, "immutable-var-args") == 0
      );
      definition->first_argument_index = declaration_name->next;
      body_index = possible_declarations->next;
      definition->body_index = body_index;
    } else {
      goto incorrect_arguments_declaration;
    }
  }

  if (
    body_index == EXPR_NONE ||
    body_index >= interpreter->expression_count ||
    interpreter->storage.expressions[body_index].next != EXPR_NONE
  ) {
    report_evaluation_failure(interpreter, "(fn) expects exactly 1 body expression");
    return false;
  }

  // Validate arguments definition (if it is provided) and compute `named_argument_count`.
  uint32_t argument_index = definition->first_argument_index;
  while (argument_index != EXPR_NONE) {
    if (
      argument_index >= interpreter->expression_count ||
      definition->named_argument_count >= EVAL_ARG_CAPACITY
    ) {
      report_evaluation_failure(interpreter, "(fn) too many named arguments");
      return false;
    }

    const Expression *argument = &interpreter->storage.expressions[argument_index];
    if (argument->kind != EXPR_STRING) {
      report_evaluation_failure(interpreter, "(fn) argument names must be string literals");
      return false;
    }

    const ExpressionAllocation *name = get_expression_allocation(interpreter, argument->data);
    char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};
    char local_name[SYMBOL_NAME_SIZE] = {'\0'};
    Datum name_value = {
      .kind = SYM_VARIABLE,
      .as.value = {
        .type = TYPE_STRING,
        .as.string = {
          .data = name == NULL ? NULL : name->data,
          .length = name == NULL ? 0 : name->length,
          .base = name == NULL ? NULL : name->data,
          .immutable_ref = false,
          .nul_terminated = name != NULL && name->data != NULL,
        },
      },
    };
    bool valid_name =
      get_variable_name(&name_value, &symbol_name) &&
      local_symbol_name(definition_index, symbol_name, &local_name);

    wipe_memory(&name_value, sizeof(name_value));
    wipe_memory(symbol_name, sizeof(symbol_name));
    wipe_memory(local_name, sizeof(local_name));

    if (!valid_name) {
      report_evaluation_failure(interpreter, "(fn) invalid or too long argument name");
      return false;
    }

    definition->named_argument_count++;
    argument_index = argument->next;
  }

  return true;

  invalid_definition:
    report_evaluation_failure(interpreter, "(fn) label does not point to a function definition");
    return false;

  incorrect_arguments_declaration:
    report_evaluation_failure(interpreter, "(fn) has incorrect arguments declaration");
    return false;
}

// Bind a declared single argument as a local in the implicit function scope.
// Ordinary values use the existing `(local)` copy semantics. Immutable
// declarations borrow string and array storage through read-only descriptors.
// Labels and `TYPE_VOID` can also be passed without a new symbol representation.
static bool bind_user_function_argument(
  Interpreter *interpreter,
  const Datum *name,
  const Datum *value,
  bool immutable_arguments
) {
  // Use `(local)` for ordinary values. Mark string and array descriptors as
  // immutable before binding so `(local)` shares their backing storage.
  // Labels and `TYPE_VOID` require direct symbol assignment.
  if (
    value->kind != SYM_LABEL &&
    !(value->kind == SYM_VARIABLE && value->as.value.type == TYPE_VOID)
  ) {
    Datum binding[] = { *name, *value };
    if (immutable_arguments) {
      if (binding[1].kind == SYM_ARRAY) {
        binding[1].as.array.immutable_ref = true;
      } else if (
        binding[1].kind == SYM_VARIABLE &&
        binding[1].as.value.type == TYPE_STRING
      ) {
        binding[1].as.value.as.string.immutable_ref = true;
      }
    }
    (void)std_local(interpreter, binding, 2);
    wipe_memory(binding, sizeof(binding));
    return interpreter->eval_outcome.kind == EVAL_OK;
  }

  // Below code only applies for labels and `TYPE_VOID` (no need for copying).

  // Construct the argument’s name within the implicit function `(do)` scope.
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};
  char local_name[SYMBOL_NAME_SIZE] = {'\0'};
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  bool ok = false;

  if (
    !get_variable_name(name, &symbol_name) ||
    !local_symbol_name(interpreter->local_scope->expression_index, symbol_name, &local_name)
  ) {
    report_evaluation_failure(interpreter, "(fn) invalid or too long argument name");
    goto finish;
  }

  // Define the local symbol if necessary, or verify that an existing one
  // can be replaced. Repeated function calls reuse the same symbol.
  Symbol *symbol = symbol_lookup(interpreter, local_name);
  if (symbol == NULL) {
    symbol = symbol_define(interpreter, local_name, value->kind);
    if (symbol == NULL) {
      extract_error_message_from_symbol_define_error(interpreter, &fail_msg, local_name);
      report_evaluation_failure(interpreter, fail_msg);
      goto finish;
    }
  } else if (!validate_symbol_mutable(symbol, "fn", &fail_msg)) {
    report_evaluation_failure(interpreter, fail_msg);
    goto finish;
  }

  // Assign labels and `TYPE_VOID` directly, preserving their original datum kind.
  // Neither requires allocating or copying dynamic data.
  symbol->kind = value->kind;
  if (value->kind == SYM_LABEL)
    symbol->as.instruction_index = value->as.instruction_index;
  else
    symbol->as.variable = value->as.value;

  // Register the argument for automatic cleanup when the function scope exits.
  register_local_symbol(interpreter, symbol);
  ok = true;

  finish:
    wipe_memory(symbol_name, sizeof(symbol_name));
    wipe_memory(local_name, sizeof(local_name));
    wipe_memory(fail_msg, sizeof(fail_msg));
    return ok;
}

// Invoke an existing function definition using the normal expression evaluator.
// The caller’s scopes and previous evaluation result remain separate from the
// invocation’s implicit `(do)` scope and its result history.
bool evaluate_user_function(
  Interpreter *interpreter,
  uint32_t definition_expression_index,
  const Datum *args,
  size_t argc,
  Datum *result
) {
  // Bound recursive calls before consuming more of the evaluator’s C stack.
  // Tail-call optimization is not implemented, so each call has its own frame.
  uint32_t call_depth = interpreter->function_call == NULL
    ? 1
    : interpreter->function_call->call_depth + 1;

  if (call_depth > EVAL_FUNCTION_CALL_DEPTH_LIMIT) {
    report_evaluation_failure(interpreter, "(fn) maximum function call depth exceeded");
    return false;
  }

  UserFunctionDefinition definition = {0};
  if (!get_user_function_definition(interpreter, definition_expression_index, &definition)) {
    wipe_memory(&definition, sizeof(definition));
    return false;
  }

  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  if (
    (definition.variadic && argc < definition.named_argument_count) ||
    (!definition.variadic && argc != definition.named_argument_count)
  ) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      definition.variadic
        ? "(fn) expects at least %zu arguments, got instead: %zu"
        : "(fn) expects exactly %zu arguments, got instead: %zu",
      definition.named_argument_count,
      argc
    );
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    wipe_memory(&definition, sizeof(definition));
    return false;
  }

  EvalOutcome previous_outcome = interpreter->eval_outcome;
  Datum previous_result = previous_outcome.result;
  PUSH_GC_ROOT(interpreter, previous_result_gc_root, &previous_result, 1);
  PUSH_GC_ROOT(interpreter, function_arguments_gc_root, args, argc);

  EvalLocalScopeFrame scope = { .expression_index = definition_expression_index };
  EvalLocalScopeFrame *previous_scope = interpreter->local_scope;
  EvalFunctionCallFrame call = {
    .definition_expression_index = definition_expression_index,
    .entry_sequence_depth = interpreter->current_sequence_depth,
    .call_depth = call_depth,
    .arguments = args,
    .argument_count = argc,
    .immutable_arguments = definition.immutable_arguments,
    .previous = interpreter->function_call,
  };

  // The function sees its own definition-site scope, not the caller’s locals.
  // Reentrant activation tracking still sees every active caller scope.
  if (!enter_local_scope(interpreter, &scope)) {
    POP_GC_ROOT(interpreter, function_arguments_gc_root);
    POP_GC_ROOT(interpreter, previous_result_gc_root);
    wipe_memory(&call, sizeof(call));
    wipe_memory(&scope, sizeof(scope));
    wipe_memory(&previous_result, sizeof(previous_result));
    wipe_memory(&previous_outcome, sizeof(previous_outcome));
    wipe_memory(&definition, sizeof(definition));
    return false;
  }

  interpreter->function_call = &call;
  interpreter->current_sequence_depth++;
  interpreter->eval_outcome.result = (Datum) { .kind = SYM_EMPTY };

  bool ok = false;
  Datum function_result = { .kind = SYM_EMPTY };
  PUSH_GC_ROOT(interpreter, function_result_gc_root, &function_result, 1);

  // The implicit `argc` is a normal local and may be shadowed by an
  // explicitly declared argument with the same name.
  const char argc_name[] = "argc";
  Datum argc_binding[] = {
    {
      .kind = SYM_VARIABLE,
      .as.value = {
        .type = TYPE_STRING,
        .as.string = {
          .data = argc_name,
          .length = sizeof(argc_name) - 1,
          .base = argc_name,
          .immutable_ref = false,
          .nul_terminated = true,
        },
      },
    },
    {
      .kind = SYM_VARIABLE,
      .as.value = { .type = TYPE_U32, .as.u32 = (uint32_t)argc },
    },
  };
  (void)std_local(interpreter, argc_binding, 2);
  wipe_memory(argc_binding, sizeof(argc_binding));
  if (interpreter->eval_outcome.kind != EVAL_OK) goto finish;

  uint32_t name_index = definition.first_argument_index;
  for (size_t i = 0; i < definition.named_argument_count; ++i) {
    const Expression *name_expr = &interpreter->storage.expressions[name_index];

    const ExpressionAllocation *allocation =
      get_expression_allocation(interpreter, name_expr->data);

    Datum name = {
      .kind = SYM_VARIABLE,
      .as.value = {
        .type = TYPE_STRING,
        .as.string = {
          .data = allocation->data,
          .length = allocation->length,
          .base = allocation->data,
          .immutable_ref = false,
          .nul_terminated = true,
        },
      },
    };

    bool bound = bind_user_function_argument(
      interpreter,
      &name,
      &args[i],
      definition.immutable_arguments
    );
    wipe_memory(&name, sizeof(name));
    if (!bound) goto finish;
    name_index = name_expr->next;
  }

  // Reuse the stored body expression. `evaluate_sequence` provides the same
  // `(leave)`, `(defer)`, `(goto)`, and return-result behavior as an ordinary `(do)`.
  ok = evaluate_sequence(interpreter, definition.body_index, &function_result, false);

  finish:
    // User defers have run. Restore any suspended recursive activation
    // after dropping this invocation’s local references (without `(gc)`).
    leave_local_scope(interpreter, &scope, previous_scope);

    interpreter->current_sequence_depth--;
    interpreter->function_call = call.previous;

    if (
      ok &&
      interpreter->eval_outcome.kind == EVAL_OK &&
      interpreter->pending_leave_count == 0 &&
      interpreter->pending_jump_index == EXPR_NONE
    ) {
      *result = function_result;
      interpreter->eval_outcome = previous_outcome;
    } else {
      ok = false;
    }

    POP_GC_ROOT(interpreter, function_result_gc_root);
    POP_GC_ROOT(interpreter, function_arguments_gc_root);
    POP_GC_ROOT(interpreter, previous_result_gc_root);

    // These frames only borrow references. Never recurse through `previous`
    // or wipe argument data, returned allocations, or the caller’s frames.
    wipe_memory(&function_result, sizeof(function_result));
    wipe_memory(&previous_result, sizeof(previous_result));
    wipe_memory(&previous_outcome, sizeof(previous_outcome));
    wipe_memory(&definition, sizeof(definition));
    wipe_memory(&call, sizeof(call));
    return ok;
}

// Evaluate a loop without synthesizing expression nodes or allocating
// per-iteration state. Every iteration creates its own implicit `(do)` scope,
// so locals and registered defers are cleaned up after that iteration.
static bool evaluate_loop(
  Interpreter *interpreter,
  uint32_t loop_expression_index,
  uint32_t condition_index,
  uint32_t body_index,
  bool is_while,
  bool indefinite,
  uint32_t count,
  Datum *result
) {
  EvalLoopFrame loop = {
    .function_call = interpreter->function_call,
    .previous = interpreter->loop_frame,
  };
  interpreter->loop_frame = &loop;

  Datum loop_result = { .kind = SYM_EMPTY };
  PUSH_GC_ROOT(interpreter, loop_result_gc_root, &loop_result, 1);
  bool ok = true;

  for (
    uint32_t iteration = 0;
    indefinite || is_while || iteration < count;
    ++iteration
  ) {
    EvalLocalScopeFrame scope = {
      .expression_index = loop_expression_index,
      .previous = interpreter->local_scope,
    };
    if (!enter_local_scope(interpreter, &scope)) {
      ok = false;
      goto finish;
    }

    interpreter->current_sequence_depth++;
    loop.sequence_depth = interpreter->current_sequence_depth;
    loop.exit_requested = false;
    uint32_t defer_start = interpreter->deferred_expression_count;
    bool execute_body = true;
    bool body_started = false;
    bool iteration_ok = true;

    if (is_while) {
      Datum condition = { .kind = SYM_EMPTY };
      PUSH_GC_ROOT(interpreter, loop_condition_gc_root, &condition, 1);
      iteration_ok = evaluate_expression(interpreter, condition_index, &condition);
      if (iteration_ok) {
        if (condition.kind != SYM_VARIABLE) {
          report_evaluation_failure(interpreter, "(while) condition must be a numeric value");
          iteration_ok = false;
        } else {
          switch (condition.as.value.type) {
            case TYPE_U32: execute_body = condition.as.value.as.u32 != 0; break;
            case TYPE_I32: execute_body = condition.as.value.as.i32 != 0; break;
            case TYPE_F32: execute_body = condition.as.value.as.f32 != 0.0f; break;
            default:
              report_evaluation_failure(interpreter, "(while) condition must be a numeric value");
              iteration_ok = false;
              break;
          }
        }
      }
      POP_GC_ROOT(interpreter, loop_condition_gc_root);
      wipe_memory(&condition, sizeof(condition));
    }

    if (iteration_ok && execute_body) {
      // `body_index` is the final argument, so its sibling chain is empty.
      // Using `evaluate_sequence` gives the loop body the usual `(goto)`,
      // `(leave)`, and `(defer)` behavior of an implicit `(do)`.
      body_started = true;
      iteration_ok = evaluate_sequence(interpreter, body_index, &loop_result, false);
    }

    if (!body_started) {
      // When the condition is false or interrupts control flow, the loop
      // scope still needs its deferred expressions to run before cleanup.
      if (interpreter->pending_leave_count != 0) {
        interpreter->pending_leave_count--;
        if (interpreter->pending_leave_count == 0) {
          loop.exit_requested = true;
          loop_result = interpreter->eval_outcome.result;
          iteration_ok = true;
        }
      }
      if (!run_deferred_expressions(interpreter, defer_start)) iteration_ok = false;
    }

    // User defers have finished. Reset iteration locals and restore the
    // enclosing scope before deciding whether to repeat or propagate control.
    interpreter->current_sequence_depth--;
    leave_local_scope(interpreter, &scope, scope.previous);

    if (!iteration_ok) {
      ok = false;
      goto finish;
    }
    if (loop.exit_requested || !execute_body) break;
  }

  *result = loop_result;

  finish:
    POP_GC_ROOT(interpreter, loop_result_gc_root);
    interpreter->loop_frame = loop.previous;
    wipe_memory(&loop, sizeof(loop));
    wipe_memory(&loop_result, sizeof(loop_result));
    return ok;
}

static bool evaluate_expression_impl(
  Interpreter *interpreter,
  uint32_t current_expression_index,
  Datum *result
) {
  if (
    current_expression_index == EXPR_NONE ||
    current_expression_index >= interpreter->expression_count
  ) {
    report_evaluation_failure(interpreter, "Invalid expression index");
    return false;
  }

  // A label used directly as an `(if)` branch is not visited by a `(do)`
  // sequence loop, so it must also be able to complete jump re-entry here.
  if (current_expression_index == interpreter->resume_target_index) {
    interpreter->resume_target_index = EXPR_NONE;
  }

  const Expression *expr = &interpreter->storage.expressions[current_expression_index];

  switch (expr->kind) {
    case EXPR_U32:
      *result = (Datum) {
        .kind = SYM_VARIABLE,
        .as.value = { .type = TYPE_U32, .as.u32 = expr->data },
      };
      return true;

    case EXPR_I32: {
      int32_t number;
      memcpy(&number, &expr->data, sizeof(number));
      *result = (Datum) {
        .kind = SYM_VARIABLE,
        .as.value = { .type = TYPE_I32, .as.i32 = number },
      };
      return true;
    }

    case EXPR_F32: {
      float number;
      memcpy(&number, &expr->data, sizeof(number));
      *result = (Datum) {
        .kind = SYM_VARIABLE,
        .as.value = { .type = TYPE_F32, .as.f32 = number },
      };
      return true;
    }

    case EXPR_STRING: {
      const ExpressionAllocation *allocation = get_expression_allocation(interpreter, expr->data);
      if (allocation == NULL || allocation->data == NULL) {
        report_evaluation_failure(interpreter, "Invalid string allocation");
        return false;
      }
      *result = (Datum) {
        .kind = SYM_VARIABLE,
        .as.value = {
          .type = TYPE_STRING,
          .as.string = {
            .data = allocation->data,
            .length = allocation->length,
            .base = allocation->data,
            .immutable_ref = false,
            .nul_terminated = true,
          },
        },
      };
      return true;
    }

    case EXPR_ARRAY: {
      const ExpressionAllocation *allocation = get_expression_allocation(interpreter, expr->data);
      if (allocation == NULL) {
        report_evaluation_failure(interpreter, "Invalid array allocation");
        return false;
      }
      *result = (Datum) {
        .kind = SYM_ARRAY,
        .as.array = {
          .element_type = allocation->element_type,
          .length = allocation->length,
          .data = allocation->data,
          .base = allocation->data,
        },
      };
      return true;
    }

    case EXPR_LIST: {
      uint32_t fn_name_index = expr->first_child;

      if (fn_name_index == EXPR_NONE || fn_name_index >= interpreter->expression_count) {
        report_evaluation_failure(interpreter, "Expression list has no valid function name");
        return false;
      }

      const Expression *fn = &interpreter->storage.expressions[fn_name_index];

      if (fn->kind != EXPR_NAME) {
        report_evaluation_failure(interpreter, "Expression function name must be a name");
        return false;
      }

      // Resolve function name from expression allocations.
      const ExpressionAllocation *allocation = get_expression_allocation(interpreter, fn->data);

      if (allocation == NULL || allocation->data == NULL) {
        report_evaluation_failure(interpreter, "Invalid function name allocation");
        return false;
      }

      const char *function_name = allocation->data;

      // Special form: register a user-defined function as an ordinary label.
      if (strcmp(function_name, "fn") == 0) {
        if (interpreter->local_scope != NULL) {
          report_evaluation_failure(interpreter, "(fn) requires top-level scope");
          return false;
        }

        UserFunctionDefinition definition = {0};
        if (!get_user_function_definition(interpreter, current_expression_index, &definition)) {
          wipe_memory(&definition, sizeof(definition));
          return false;
        }

        // Evaluate name of the function (first argument, in case it is an expression).
        Datum previous_result = interpreter->eval_outcome.result;
        PUSH_GC_ROOT(interpreter, definition_result_gc_root, &previous_result, 1);
        Datum name_value = { .kind = SYM_EMPTY };
        char name[SYMBOL_NAME_SIZE] = {'\0'};
        char label_name[SYMBOL_NAME_SIZE] = {'\0'};
        bool definition_ok = false;

        if (!evaluate_expression(interpreter, definition.name_index, &name_value)) {
          goto fn_definition_finish;
        }

        // Read function name.
        if (!get_variable_name(&name_value, &name)) {
          report_evaluation_failure(interpreter, "(fn) invalid function name");
          goto fn_definition_finish;
        }

        // Generate a label name.
        int name_length = snprintf(
          label_name,
          sizeof(label_name),
          EVAL_FN_LABEL_FORMAT_TEMPLATE,
          name
        );
        if (name_length <= 0 || (size_t)name_length >= sizeof(label_name)) {
          report_evaluation_failure(interpreter, "(fn) function label name is too long");
          goto fn_definition_finish;
        }

        Symbol *symbol = symbol_lookup(interpreter, label_name);
        if (symbol == NULL) {
          symbol = symbol_define(interpreter, label_name, SYM_LABEL);
          if (symbol == NULL) {
            char fail_msg[ERR_MSG_SIZE] = {'\0'};
            extract_error_message_from_symbol_define_error(interpreter, &fail_msg, label_name);
            report_evaluation_failure(interpreter, fail_msg);
            wipe_memory(fail_msg, sizeof(fail_msg));
            goto fn_definition_finish;
          }
        } else if (symbol->kind != SYM_LABEL) {
          report_evaluation_failure(interpreter, "(fn) function symbol is not a label");
          goto fn_definition_finish;
        }

        if (symbol->frozen && symbol->as.instruction_index != current_expression_index) {
          report_evaluation_failure(interpreter, "(fn) cannot redefine a frozen label");
          goto fn_definition_finish;
        }

        if (!symbol->frozen) symbol->as.instruction_index = current_expression_index;
        *result = previous_result;
        definition_ok = true;

        fn_definition_finish:
          POP_GC_ROOT(interpreter, definition_result_gc_root);
          wipe_memory(&definition, sizeof(definition));
          wipe_memory(&name_value, sizeof(name_value));
          wipe_memory(&previous_result, sizeof(previous_result));
          wipe_memory(name, sizeof(name));
          wipe_memory(label_name, sizeof(label_name));
          return definition_ok;
      }

      // Special forms: direct evaluator loops with per-iteration `(do)` scopes.
      if (strcmp(function_name, "while") == 0 || strcmp(function_name, "repeat") == 0) {
        bool is_while = strcmp(function_name, "while") == 0;
        uint32_t first_index = fn->next;
        if (first_index == EXPR_NONE || first_index >= interpreter->expression_count) {
          report_evaluation_failure(
            interpreter,
            is_while
              ? "(while) expects exactly 2 expressions"
              : "(repeat) expects 1 or 2 expressions"
          );
          return false;
        }

        uint32_t second_index = interpreter->storage.expressions[first_index].next;
        if (
          (is_while && second_index == EXPR_NONE) ||
          (
            second_index != EXPR_NONE &&
            (
              second_index >= interpreter->expression_count ||
              interpreter->storage.expressions[second_index].next != EXPR_NONE
            )
          )
        ) {
          report_evaluation_failure(
            interpreter,
            is_while
              ? "(while) expects exactly 2 expressions"
              : "(repeat) expects 1 or 2 expressions"
          );
          return false;
        }

        if (is_while)
          // Conditional `(while)`.
          return evaluate_loop(
            interpreter,
            current_expression_index,
            first_index, // Condition index
            second_index, // Body index
            true, // Is `(while)`
            false, // Is not indefinite
            0, // Amount of iterations (not related to `(while)`)
            result
          );

        // A single `(repeat EXPR)` loops indefinitely. In the two-argument
        // form `(repeat N EXPR)`, evaluate N once before running the body.
        if (second_index == EXPR_NONE)
          // Indefinite `(repeat)`.
          return evaluate_loop(
            interpreter,
            current_expression_index,
            EXPR_NONE, // Condition index (`(repeat)` does not have it)
            first_index, // Body index
            false, // Is not `(while)`
            true, // Is indefinite
            0, // Amount of iterations (not related when is indefinite)
            result
          );

        Datum count = { .kind = SYM_EMPTY };
        if (!evaluate_expression(interpreter, first_index, &count)) {
          wipe_memory(&count, sizeof(count));
          return false;
        }
        if (count.kind != SYM_VARIABLE || count.as.value.type != TYPE_U32) {
          report_evaluation_failure(interpreter, "(repeat) count must be u32");
          wipe_memory(&count, sizeof(count));
          return false;
        }
        uint32_t repetitions = count.as.value.as.u32;
        wipe_memory(&count, sizeof(count));

        // Fininte `(repeat N)`.
        return evaluate_loop(
          interpreter,
          current_expression_index,
          EXPR_NONE, // Condition index (`(repeat)` does not have it)
          second_index, // Body index
          false, // Is not `(while)`
          false, // Is not indefinite (only N/`repetitions` iterations)
          repetitions, // Amount of iterations
          result
        );
      }

      // Special form: boolean conjunction and disjunction with short-circuiting.
      if (strcmp(function_name, "and") == 0 || strcmp(function_name, "or") == 0) {
        bool is_and = strcmp(function_name, "and") == 0;
        bool boolean_result = is_and; // `(and)` → true, `(or)` → false

        uint32_t arg_index = fn->next;

        while (arg_index != EXPR_NONE) {
          if (arg_index >= interpreter->expression_count) {
            report_evaluation_failure(interpreter, "Invalid argument expression index");
            return false;
          }

          Datum condition = { .kind = SYM_EMPTY };
          bool condition_ok = evaluate_expression(interpreter, arg_index, &condition);
          if (!condition_ok) {
            wipe_memory(&condition, sizeof(condition));
            return false;
          }

          if (condition.kind != SYM_VARIABLE) {
            report_evaluation_failure(
              interpreter,
              is_and
                ? "(and) condition must be a numeric value"
                : "(or) condition must be a numeric value"
            );
            wipe_memory(&condition, sizeof(condition));
            return false;
          }

          bool is_true;
          switch (condition.as.value.type) {
            case TYPE_U32: is_true = condition.as.value.as.u32 != 0; break;
            case TYPE_I32: is_true = condition.as.value.as.i32 != 0; break;
            case TYPE_F32: is_true = condition.as.value.as.f32 != 0.0f; break;
            default:
              report_evaluation_failure(
                interpreter,
                is_and
                  ? "(and) condition must be a numeric value"
                  : "(or) condition must be a numeric value"
              );
              wipe_memory(&condition, sizeof(condition));
              return false;
          }

          wipe_memory(&condition, sizeof(condition));
          boolean_result = is_true;

          // Stop at the first false for `(and)`, or first true for `(or)`.
          if (is_and ? !is_true : is_true) break;

          arg_index = interpreter->storage.expressions[arg_index].next;
        }

        *result = (Datum) {
          .kind = SYM_VARIABLE,
          .as.value = {
            .type = TYPE_U32,
            .as.u32 = boolean_result ? 1u : 0u,
          },
        };

        return true;
      }

      // Special form: evaluate the condition, then only the selected branch.
      if (strcmp(function_name, "if") == 0) {
        // Require a condition and a true branch; false branch is optional.
        uint32_t condition_index = fn->next;

        if (condition_index == EXPR_NONE || condition_index >= interpreter->expression_count) {
          report_evaluation_failure(interpreter, "(if) expects 2 or 3 arguments");
          return false;
        }

        uint32_t true_index = interpreter->storage.expressions[condition_index].next;

        if (true_index == EXPR_NONE || true_index >= interpreter->expression_count) {
          report_evaluation_failure(interpreter, "(if) expects 2 or 3 arguments");
          return false;
        }

        uint32_t false_index = interpreter->storage.expressions[true_index].next;

        if (false_index != EXPR_NONE) {
          if (
            false_index >= interpreter->expression_count ||
            interpreter->storage.expressions[false_index].next != EXPR_NONE
          ) {
            report_evaluation_failure(interpreter, "(if) expects 2 or 3 arguments");
            return false;
          }
        }

        // A goto entering an `(if)` branch is a transfer to that branch's
        // label, not a fresh conditional evaluation. Follow the path to the
        // label even if the condition has changed since the label was defined.
        if (interpreter->resume_target_index != EXPR_NONE) {
          if (contains_jump_target(interpreter, true_index, interpreter->resume_target_index)) {
            return evaluate_expression(interpreter, true_index, result);
          }
          if (contains_jump_target(interpreter, false_index, interpreter->resume_target_index)) {
            return evaluate_expression(interpreter, false_index, result);
          }
        }

        // Evaluate only the condition first.
        Datum condition = { .kind = SYM_EMPTY };

        bool condition_ok = evaluate_expression(interpreter, condition_index, &condition);
        if (!condition_ok) {
          wipe_memory(&condition, sizeof(condition));
          return false;
        }

        if (condition.kind != SYM_VARIABLE) {
          report_evaluation_failure(interpreter, "(if) condition must be a numeric value");
          wipe_memory(&condition, sizeof(condition));
          return false;
        }

        bool is_true;

        switch (condition.as.value.type) {
          case TYPE_U32: is_true = condition.as.value.as.u32 != 0; break;
          case TYPE_I32: is_true = condition.as.value.as.i32 != 0; break;
          case TYPE_F32: is_true = condition.as.value.as.f32 != 0.0f; break;
          default:
            report_evaluation_failure(interpreter, "(if) condition must be a numeric value");
            wipe_memory(&condition, sizeof(condition));
            return false;
        }

        wipe_memory(&condition, sizeof(condition));

        // Evaluate only the selected branch, if present.
        uint32_t selected_index = is_true ? true_index : false_index;

        // In case there is no false-branch just return `SYM_EMPTY`
        if (selected_index == EXPR_NONE) {
          *result = (Datum) { .kind = SYM_EMPTY };
          return true;
        }

        return evaluate_expression(interpreter, selected_index, result);
      }

      // Special form: defer evaluation until the enclosing `(do)` terminates.
      if (strcmp(function_name, "defer") == 0) {
        uint32_t deferred_index = fn->next;

        if (
          deferred_index == EXPR_NONE ||
          deferred_index >= interpreter->expression_count ||
          interpreter->storage.expressions[deferred_index].next != EXPR_NONE
        ) {
          report_evaluation_failure(interpreter, "(defer) expects exactly 1 expression");
          return false;
        }

        if (interpreter->current_sequence_depth == 0) {
          report_evaluation_failure(interpreter, "(defer) requires an active (do) sequence");
          return false;
        }

        // A deferred expression may create and use nested `(do)` sequences,
        // but it cannot register another defer in the block already exiting.
        if (interpreter->executing_deferred_depth == interpreter->current_sequence_depth) {
          report_evaluation_failure(
            interpreter,
            "(defer) cannot register in an exiting (do) sequence"
          );
          return false;
        }

        if (
          interpreter->deferred_expression_count >=
          interpreter->storage.deferred_expression_capacity
        ) {
          report_evaluation_failure(interpreter, "(defer) registration capacity reached");
          return false;
        }

        interpreter->storage.deferred_expression_indices[
          interpreter->deferred_expression_count
        ] = deferred_index;
        interpreter->deferred_expression_count++;

        // Registration does not evaluate the argument or replace the last value.
        *result = interpreter->eval_outcome.result;
        return true;
      }

      // Special form: leave one or more enclosing `(do)` sequences.
      if (strcmp(function_name, "leave") == 0) {
        uint32_t arg_index = fn->next;
        uint32_t value_index = EXPR_NONE;

        if (arg_index != EXPR_NONE) {
          if (arg_index >= interpreter->expression_count) {
            report_evaluation_failure(interpreter, "(leave) expects 0, 1, or 2 arguments");
            return false;
          }

          value_index = interpreter->storage.expressions[arg_index].next;
          if (
            value_index != EXPR_NONE &&
            (
              value_index >= interpreter->expression_count ||
              interpreter->storage.expressions[value_index].next != EXPR_NONE
            )
          ) {
            report_evaluation_failure(interpreter, "(leave) expects 0, 1, or 2 arguments");
            return false;
          }
        }

        if (interpreter->current_sequence_depth == 0) {
          report_evaluation_failure(interpreter, "(leave) requires an active (do) sequence");
          return false;
        }

        uint32_t leave_count = 1;

        if (arg_index != EXPR_NONE) {
          Datum arg = { .kind = SYM_EMPTY };
          bool arg_ok = evaluate_expression(interpreter, arg_index, &arg);
          if (!arg_ok) {
            wipe_memory(&arg, sizeof(arg));
            return false;
          }

          if (arg.kind != SYM_VARIABLE || arg.as.value.type != TYPE_U32) {
            report_evaluation_failure(interpreter, "(leave) argument must be u32");
            wipe_memory(&arg, sizeof(arg));
            return false;
          }

          leave_count = arg.as.value.as.u32;
          wipe_memory(&arg, sizeof(arg));
        }

        if (leave_count == 0) {
          report_evaluation_failure(interpreter, "(leave) count must be greater than 0");
          return false;
        }

        if (leave_count > interpreter->current_sequence_depth) {
          report_evaluation_failure(interpreter, "(leave) count exceeds current (depth)");
          return false;
        }

        if (value_index != EXPR_NONE) {
          Datum value = { .kind = SYM_EMPTY };
          bool value_ok = evaluate_expression(interpreter, value_index, &value);
          if (!value_ok) {
            wipe_memory(&value, sizeof(value));
            return false;
          }
          interpreter->eval_outcome.result = value;
          wipe_memory(&value, sizeof(value));
        }

        interpreter->pending_leave_count = leave_count;
        return false;
      }

      // Special form: return from the current user-defined function.
      // This uses the same scope unwinding as `(leave (fn-return-depth))`,
      // but omitting the value explicitly returns `SYM_EMPTY`.
      if (strcmp(function_name, "return") == 0) {
        uint32_t value_index = fn->next;

        if (
          value_index != EXPR_NONE &&
          (
            value_index >= interpreter->expression_count ||
            interpreter->storage.expressions[value_index].next != EXPR_NONE
          )
        ) {
          report_evaluation_failure(interpreter, "(return) expects 0 or 1 argument");
          return false;
        }

        if (interpreter->function_call == NULL) {
          report_evaluation_failure(interpreter, "(return) requires an active (fn) call");
          return false;
        }

        // Evaluate the optional value before requesting a return, so a nested
        // control transfer or evaluation failure takes precedence.
        Datum value = { .kind = SYM_EMPTY };
        if (value_index != EXPR_NONE) {
          if (!evaluate_expression(interpreter, value_index, &value)) {
            wipe_memory(&value, sizeof(value));
            return false;
          }
        }

        interpreter->eval_outcome.result = value;
        wipe_memory(&value, sizeof(value));

        // The implicit function scope is included in the leave count.
        // Do not unwind caller scopes, even during nested or recursive calls.
        interpreter->pending_leave_count = get_current_function_return_depth(interpreter);
        return false;
      }

      // Special form: evaluate child expressions sequentially.
      if (strcmp(function_name, "do") == 0) {
        EvalLocalScopeFrame scope = {
          .expression_index = current_expression_index,
          .previous = interpreter->local_scope,
        };
        if (!enter_local_scope(interpreter, &scope)) {
          wipe_memory(&scope, sizeof(scope));
          return false;
        }

        interpreter->current_sequence_depth++;
        bool ok = evaluate_sequence(interpreter, fn->next, result, false);

        // User `(defer)` expressions have run. Drop the local references
        // without collecting memory, then restore an older activation’s locals.
        interpreter->current_sequence_depth--;
        leave_local_scope(interpreter, &scope, scope.previous);
        return ok;
      }

      // Public `_FN_` labels (user-defined functions) take precedence over native functions.
      char label_name[SYMBOL_NAME_SIZE] = {'\0'};
      int label_length = snprintf(
        label_name,
        sizeof(label_name),
        EVAL_FN_LABEL_FORMAT_TEMPLATE,
        function_name
      );

      // If the function name including the user-defined function label template
      // parts exceeds the name limit it means it is not a user-defined function
      // (since user could not define a function with a name that long).
      const Symbol *function_label =
        label_length > 0 && (size_t)label_length < sizeof(label_name)
          ? symbol_lookup(interpreter, label_name)
          : NULL;

      wipe_memory(label_name, sizeof(label_name));
      const bool is_user_function = function_label != NULL && function_label->kind == SYM_LABEL;
      const Symbol *symbol = is_user_function ? NULL : symbol_lookup(interpreter, function_name);

      if (!is_user_function) {
        if (symbol == NULL) {
          report_evaluation_failure(interpreter, "Undefined function");
          return false;
        } else if (symbol->kind != SYM_NATIVE_FUNCTION) {
          report_evaluation_failure(interpreter, "Symbol is not a native function");
          return false;
        }
      }

      // Evaluate all arguments before calling the function.
      Datum args[EVAL_ARG_CAPACITY] = {0};

      // Earlier evaluated arguments may hold runtime values while a later
      // argument invokes (gc). Keep the entire zero-initialized array rooted.
      PUSH_GC_ROOT(interpreter, args_gc_roots, args, EVAL_ARG_CAPACITY);

      bool native_call_ok = false;
      size_t argc = 0;
      uint32_t arg_index = fn->next;

      while (arg_index != EXPR_NONE) {
        if (arg_index >= interpreter->expression_count) {
          report_evaluation_failure(interpreter, "Invalid argument expression index");
          goto native_call_finish;
        } else if (argc >= EVAL_ARG_CAPACITY) {
          report_evaluation_failure(interpreter, "Too many function arguments");
          goto native_call_finish;
        }

        uint32_t next_index = interpreter->storage.expressions[arg_index].next;

        // Evaluate child expressions recursively
        if (!evaluate_expression(interpreter, arg_index, &args[argc])) {
          goto native_call_finish;
        }

        ++argc;
        arg_index = next_index;
      }

      // Temporarily expose the calling expression's index to the native function.
      // Save and restore the previous index to support nested native calls.
      uint32_t previous_call_index = interpreter->current_native_call_expression_index;

      // Make the current expression location available to the native function.
      interpreter->current_native_call_expression_index = current_expression_index;

      // Invoke the stored body directly or dispatch to the native function.
      bool call_ok = true;
      if (is_user_function) {
        call_ok = evaluate_user_function(
          interpreter,
          function_label->as.instruction_index,
          args,
          argc,
          result
        );
      } else {
        *result = symbol->as.native_function(interpreter, args, argc);
      }

      // Restore the previous call location after the native function returns.
      interpreter->current_native_call_expression_index = previous_call_index;

      // Why restore the previous value?
      //
      // Consider a hypothetical nested native call:
      // 1. Function A is executing at expression index 10.
      // 2. Function A invokes function B at expression index 25.
      // 3. B needs to see 25 as its current call location.
      // 4. When B returns, A should see 10 again.
      //
      // Without restoring the previous index, A would incorrectly see 25.

      // Stop current evaluation when a jump, exit, or failure is requested.
      // In case of a jump this helps to avoid growing recursion.
      // Native functions report failures through the evaluation state.
      native_call_ok = (
        call_ok &&
        interpreter->eval_outcome.kind == EVAL_OK &&
        interpreter->pending_jump_index == EXPR_NONE &&
        interpreter->pending_leave_count == 0
      );

      native_call_finish:
        POP_GC_ROOT(interpreter, args_gc_roots);
        wipe_memory(args, sizeof(args));
        return native_call_ok;
    }

    // Resolving variable reference by name
    case EXPR_NAME: {
      const ExpressionAllocation *allocation = get_expression_allocation(interpreter, expr->data);

      if (
        allocation == NULL ||
        allocation->data == NULL ||
        allocation->length >= allocation->size ||
        ((const char *)allocation->data)[allocation->length] != '\0'
      ) {
        report_evaluation_failure(interpreter, "Invalid expression name allocation");
        return false;
      }

      const char *symbol_name = allocation->data;

      // `empty` always denotes the absence of a result. It cannot be an
      // ordinary symbol: SYM_EMPTY also marks vacant symbol-table slots.
      if (strcmp(symbol_name, "empty") == 0) {
        *result = (Datum) { .kind = SYM_EMPTY };
        return true;
      }

      const Symbol *symbol = symbol_lookup_scoped(interpreter, symbol_name);

      if (symbol == NULL) {
        report_evaluation_failure(interpreter, "Undefined variable");
        return false;
      }

      switch (symbol->kind) {
        case SYM_VARIABLE:
          *result = (Datum) { .kind = SYM_VARIABLE, .as.value = symbol->as.variable };
          return true;
        case SYM_ARRAY:
          *result = (Datum) { .kind = SYM_ARRAY, .as.array = symbol->as.array };
          return true;
        case SYM_LABEL:
          *result = (Datum) {
            .kind = SYM_LABEL,
            .as.instruction_index = symbol->as.instruction_index,
          };
          return true;
        default:
          report_evaluation_failure(interpreter, "Symbol is not a variable or array");
          return false;
      }
    }

    default:
      report_evaluation_failure(interpreter, "Unsupported expression kind");
      return false;
  }
}

/*
  Evaluate an expression.

  Set `settings_cb` and `state` to `NULL` if you don’t need to modify any settings.
  `state` is forwarded to `settings_cb` via `EvalSettingsInterface`
  (see `EvalSettingsInterface::state`). Note that in this case only basic stdlib
  functions will be provided (no platform-specific IO, like `(println)`).
*/
EvalOutcome evaluate(
  Interpreter *interpreter,
  void (*settings_cb)(EvalSettingsInterface*),
  void *state
) {
  if (
    interpreter == NULL ||
    !interpreter->configured ||
    interpreter->storage.expressions == NULL ||
    interpreter->storage.expression_allocations == NULL ||
    interpreter->storage.symbols == NULL ||
    interpreter->storage.runtime_allocations == NULL ||
    interpreter->storage.deferred_expression_indices == NULL
  )
    return (EvalOutcome) {
      .kind = EVAL_FAIL,
      .result = { .kind = SYM_EMPTY },
      .failure_expression_index = EXPR_NONE,
      .as.fail_msg = "Interpreter storage is not configured or expression table mismatch",
    };

  // Reset symbols
  clean_symbols(interpreter);

  // Wipe leftovers of previous evaluation
  clean_evaluation(interpreter);

  // Reset evaluation state
  interpreter->eval_outcome = (EvalOutcome) {
    .kind = EVAL_OK,
    .result = { .kind = SYM_EMPTY },
    .failure_expression_index = EXPR_NONE,
  };

  EvalSettingsInterfaceContext settings_ctx = {
    .interpreter = interpreter,
    .is_failed = false,
    .fail_msg = {'\0'},
  };

  char fail_prefix[64] = {'\0'};

  snprintf(settings_ctx.fail_msg, sizeof(settings_ctx.fail_msg), "populate_std failed: ");
  if (!populate_stdlib(interpreter, &settings_ctx.fail_msg)) goto fail;

  EvalSettingsInterface settings_interface = {
    .context = &settings_ctx,
    .state = state,
    .interpreter = interpreter,
    .define_symbol = define_symbol_interface,
    .symbol_lookup = symbol_lookup_interface,
    .report_failure = report_failure_interface,
  };

  if (settings_cb != NULL) {
    snprintf(settings_ctx.fail_msg, sizeof(settings_ctx.fail_msg), "EvalSettingsCallback failed: ");
    settings_cb(&settings_interface);
    if (settings_ctx.is_failed) {
      wipe_memory(&settings_interface, sizeof(settings_interface));
      goto fail;
    }
  }
  wipe_memory(&settings_interface, sizeof(settings_interface));
  wipe_memory(&settings_ctx, sizeof(settings_ctx));

  if (interpreter->expression_count != 0) {
    Datum result_value = { .kind = SYM_EMPTY };

    // Evaluate all top-level expressions sequentially.
    // Discard returned boolean, the result is already reported to `eval_outcome`.
    (void)evaluate_sequence(interpreter, 0, &result_value, true);

    wipe_memory(&result_value, sizeof(result_value));
  }

  wipe_memory(fail_prefix, sizeof(fail_prefix));
  return interpreter->eval_outcome;

  fail:
    EvalOutcome result = {
      .kind = EVAL_FAIL,
      .result = { .kind = SYM_EMPTY },
      .failure_expression_index = EXPR_NONE,
      .as.fail_msg = {'\0'},
    };
    snprintf(
      result.as.fail_msg,
      sizeof(result.as.fail_msg),
      "%s%s",
      fail_prefix,
      settings_ctx.fail_msg
    );
    wipe_memory(&settings_ctx, sizeof(settings_ctx));
    wipe_memory(fail_prefix, sizeof(fail_prefix));
    return result;
}

void clean_evaluation(Interpreter *interpreter) {
  // Active stack frames must have been popped by the evaluator already.
  // Wipe only these borrowed pointer slots, not the frames they once linked.
  wipe_memory(&interpreter->gc_roots, sizeof(interpreter->gc_roots));
  interpreter->gc_roots = NULL;

  // (label) & (goto)
  interpreter->pending_jump_index = EXPR_NONE;
  interpreter->pending_jump_expression_index = EXPR_NONE;
  interpreter->resume_target_index = EXPR_NONE;
  interpreter->current_native_call_expression_index = EXPR_NONE;

  #if EVAL_JUMP_CACHE_ENTRY_COUNT > 0
    wipe_memory(interpreter->jump_cache, sizeof(interpreter->jump_cache));
    wipe_memory(interpreter->jump_cache_replace_way, sizeof(interpreter->jump_cache_replace_way));
  #endif

  // (do) scopes and (depth)
  wipe_memory(&interpreter->local_scope, sizeof(interpreter->local_scope));
  wipe_memory(&interpreter->active_local_scope, sizeof(interpreter->active_local_scope));
  wipe_memory(&interpreter->function_call, sizeof(interpreter->function_call));
  wipe_memory(&interpreter->loop_frame, sizeof(interpreter->loop_frame));
  interpreter->local_scope = NULL;
  interpreter->active_local_scope = NULL;
  interpreter->function_call = NULL;
  interpreter->loop_frame = NULL;
  interpreter->current_sequence_depth = 0;

  // (leave)
  interpreter->pending_leave_count = 0;

  // (defer)
  interpreter->deferred_expression_count = 0;
  interpreter->executing_deferred_depth = 0;
  if (interpreter->storage.deferred_expression_indices != NULL)
    wipe_memory(
      interpreter->storage.deferred_expression_indices,
      (size_t)interpreter->storage.deferred_expression_capacity
        * sizeof(*interpreter->storage.deferred_expression_indices)
    );

  // Runtime allocations
  if (interpreter->storage.runtime_allocations != NULL) {
    for (uint32_t i = 0; i < interpreter->runtime_allocation_count; ++i) {
      EvalRuntimeAllocation *allocation = &interpreter->storage.runtime_allocations[i];
      wipe_memory(allocation->data, allocation->size);
      free(allocation->data);
    }
    wipe_memory(
      interpreter->storage.runtime_allocations,
      (size_t)interpreter->storage.runtime_allocation_capacity
        * sizeof(*interpreter->storage.runtime_allocations)
    );
  }
  interpreter->runtime_allocation_count = 0;

  // Eval outcome
  wipe_memory(&interpreter->eval_outcome, sizeof(interpreter->eval_outcome));
}

void* allocate_runtime_data(Interpreter *interpreter, size_t size) {
  if (
    interpreter->storage.runtime_allocations == NULL ||
    interpreter->runtime_allocation_count >= interpreter->storage.runtime_allocation_capacity
  ) return NULL;

  // Allow zero-length objects without relying on malloc(0).
  size_t allocated_size = size == 0 ? 1 : size;
  void *data = malloc(allocated_size);
  if (data == NULL) return NULL;

  interpreter->storage.runtime_allocations[
    interpreter->runtime_allocation_count
  ] = (EvalRuntimeAllocation) {
    .data = data,
    .size = allocated_size,
    .marked = false,
  };

  interpreter->runtime_allocation_count++;
  return data;
}

// Adopt an existing allocation without copying it. This allows native
// functions to grow a temporary buffer and return it as runtime-owned data.
bool adopt_runtime_data(Interpreter *interpreter, void *data, size_t size) {
  if (
    data == NULL || size == 0 ||
    interpreter->storage.runtime_allocations == NULL ||
    interpreter->runtime_allocation_count >= interpreter->storage.runtime_allocation_capacity
  ) return false;

  interpreter->storage.runtime_allocations[interpreter->runtime_allocation_count] =
    (EvalRuntimeAllocation) { .data = data, .size = size, .marked = false };
  interpreter->runtime_allocation_count++;
  return true;
}
