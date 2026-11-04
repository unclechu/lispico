#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "evaluate.h"
#include "symbol-error.h"
#include "stdlib.h"
#include "symbol.h"
#include "wipe-memory.h"
#include "storage.h"

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

// Find the expression to start executing in a sequence to reach a jump target.
//
// If the target belongs directly to the sequence, return its index.
// If the target is inside a nested `(do)`, return the containing `(do)` expression.
// Returns `EXPR_NONE` when the target cannot be reached through this sequence.
static uint32_t find_sequence_jump_entry(
  Interpreter *interpreter,
  uint32_t first_expression_index,
  uint32_t target_index
) {
  uint32_t index = first_expression_index;

  while (index != EXPR_NONE) {
    // Incorrect index
    if (index >= interpreter->expression_count) return EXPR_NONE;

    // The target `(label)` is found
    if (index == target_index) return index;

    const Expression *expr = &interpreter->storage.expressions[index];

    if (expr->kind == EXPR_LIST) {
      uint32_t fn_index = expr->first_child;

      if (fn_index != EXPR_NONE && fn_index < interpreter->expression_count) {
        const Expression *fn = &interpreter->storage.expressions[fn_index];

        if (fn->kind == EXPR_NAME) {
          const ExpressionAllocation *allocation = get_expression_allocation(interpreter, fn->data);

          if (
            allocation != NULL &&
            allocation->data != NULL &&
            strcmp(allocation->data, "do") == 0
          ) {
            // Search nested `(do)` expressions, without evaluating anything.
            uint32_t nested_entry = find_sequence_jump_entry(interpreter, fn->next, target_index);
            // Target jump label is found, return the index of the `(do)` sequence containing it
            if (nested_entry != EXPR_NONE) return index;
          }
        }
      }
    }

    index = expr->next;
  }

  return EXPR_NONE;
}

static bool evaluate_expression(
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

    // Execute each deferred expression independently of the control flow
    // that caused this sequence to terminate. Each (defer) sees its original
    // sequence state (sort of, basically just evaluation result, (goto) and
    // (leave) jumps are reset). Other (defer)s do not affect state of the others.
    // N.B. Not considering variables (they can be mutated, and different
    // (defer)s will observe these mutations).
    interpreter->eval_outcome = saved_outcome;
    interpreter->eval_outcome.kind = EVAL_OK;
    interpreter->eval_outcome.result = saved_result;
    interpreter->pending_jump_index = EXPR_NONE;
    interpreter->resume_target_index = EXPR_NONE;
    interpreter->pending_leave_count = 0;

    // Evaluate the (defer) expression
    Datum ignored_result = { .kind = SYM_EMPTY };
    bool ok = evaluate_expression(interpreter, expression_index, &ignored_result);

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
  interpreter->resume_target_index = saved_resume_index;
  interpreter->pending_leave_count = saved_leave_count;

  if (deferred_failure) {
    // A deferred failure cancels pending jumps, leaves, or exits.
    // The evaluation failure is already stored in `eval_outcome`.
    interpreter->pending_jump_index = EXPR_NONE;
    interpreter->resume_target_index = EXPR_NONE;
    interpreter->pending_leave_count = 0;
  }

  POP_GC_ROOT(interpreter, saved_eval_outcome_result);
  return !deferred_failure;
}

// Evaluate top-level or `(do)` expressions sequence.
// Also supports jumping into inactive nested `(do)` sequences.
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
      ok = false;
      goto finish;
    }
  }

  while (current_expression_index != EXPR_NONE) {
    if (current_expression_index >= interpreter->expression_count) {
      report_evaluation_failure(interpreter, "Invalid expression index");
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
        // either directly or through one of its nested (do) expressions.
        uint32_t jump_entry = find_sequence_jump_entry(
          interpreter,
          first_expression_index,
          interpreter->pending_jump_index
        );

        if (jump_entry != EXPR_NONE) {
          // A direct jump needs no nested re-entry.
          // Otherwise remember the final target while entering nested (do)s.
          interpreter->resume_target_index =
            jump_entry == interpreter->pending_jump_index
              ? EXPR_NONE
              : interpreter->pending_jump_index;

          interpreter->pending_jump_index = EXPR_NONE;
          current_expression_index = jump_entry;
          continue;
        }

        // Otherwise let the enclosing sequence try to handle the jump.
        // Only the top-level sequence reports an unresolved destination.
        if (is_top_level) {
          interpreter->pending_jump_index = EXPR_NONE;
          report_evaluation_failure(
            interpreter,
            "(goto) destination is not reachable through (do)"
          );
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

static bool evaluate_expression(
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
          if (!evaluate_expression(interpreter, arg_index, &condition)) return false;

          if (condition.kind != SYM_VARIABLE) {
            report_evaluation_failure(
              interpreter,
              is_and
                ? "(and) condition must be a numeric value"
                : "(or) condition must be a numeric value"
            );
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
              return false;
          }

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

        // Evaluate only the condition first.
        Datum condition = { .kind = SYM_EMPTY };

        if (!evaluate_expression(interpreter, condition_index, &condition)) return false;

        if (condition.kind != SYM_VARIABLE) {
          report_evaluation_failure(interpreter, "(if) condition must be a numeric value");
          return false;
        }

        bool is_true;

        switch (condition.as.value.type) {
          case TYPE_U32: is_true = condition.as.value.as.u32 != 0; break;
          case TYPE_I32: is_true = condition.as.value.as.i32 != 0; break;
          case TYPE_F32: is_true = condition.as.value.as.f32 != 0.0f; break;
          default:
            report_evaluation_failure(interpreter, "(if) condition must be a numeric value");
            return false;
        }

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

        if (
          arg_index != EXPR_NONE &&
          (
            arg_index >= interpreter->expression_count ||
            interpreter->storage.expressions[arg_index].next != EXPR_NONE
          )
        ) {
          report_evaluation_failure(interpreter, "(leave) expects 0 or 1 arguments");
          return false;
        }

        if (interpreter->current_sequence_depth == 0) {
          report_evaluation_failure(interpreter, "(leave) requires an active (do) sequence");
          return false;
        }

        uint32_t leave_count = 1;

        if (arg_index != EXPR_NONE) {
          Datum arg = { .kind = SYM_EMPTY };
          if (!evaluate_expression(interpreter, arg_index, &arg)) return false;

          if (arg.kind != SYM_VARIABLE || arg.as.value.type != TYPE_U32) {
            report_evaluation_failure(interpreter, "(leave) argument must be u32");
            return false;
          }

          leave_count = arg.as.value.as.u32;
        }

        if (leave_count == 0) {
          report_evaluation_failure(interpreter, "(leave) count must be greater than 0");
          return false;
        }

        if (leave_count > interpreter->current_sequence_depth) {
          report_evaluation_failure(interpreter, "(leave) count exceeds current (depth)");
          return false;
        }

        interpreter->pending_leave_count = leave_count;
        return false;
      }

      // Special form: evaluate child expressions sequentially.
      if (strcmp(function_name, "do") == 0) {
        interpreter->current_sequence_depth++;
        bool ok = evaluate_sequence(interpreter, fn->next, result, false);
        interpreter->current_sequence_depth--;
        return ok;
      }

      // Resolve the function from the existing global symbol table.
      const Symbol *symbol = symbol_lookup(interpreter, function_name);

      if (symbol == NULL) {
        report_evaluation_failure(interpreter, "Undefined function");
        return false;
      } else if (symbol->kind != SYM_NATIVE_FUNCTION) {
        report_evaluation_failure(interpreter, "Symbol is not a native function");
        return false;
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

      // Execute the native function (e.g. `std_label` reads this location).
      *result = symbol->as.native_function(interpreter, args, argc);

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
        interpreter->eval_outcome.kind == EVAL_OK &&
        interpreter->pending_jump_index == EXPR_NONE
      );

      native_call_finish:
        POP_GC_ROOT(interpreter, args_gc_roots);
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
      const Symbol *symbol = symbol_lookup(interpreter, symbol_name);

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
    if (settings_ctx.is_failed) goto fail;
    wipe_memory(&settings_ctx, sizeof(settings_ctx));
  }

  if (interpreter->expression_count != 0) {
    Datum result_value = { .kind = SYM_EMPTY };

    // Evaluate all top-level expressions sequentially.
    // Discard returned boolean, the result is already reported to `eval_outcome`.
    (void)evaluate_sequence(interpreter, 0, &result_value, true);

    wipe_memory(&result_value, sizeof(result_value));
  }

  return interpreter->eval_outcome;

  fail:
    EvalOutcome result = {
      .kind = EVAL_FAIL,
      .result = { .kind = SYM_EMPTY },
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
    return result;
}

void clean_evaluation(Interpreter *interpreter) {
  interpreter->gc_roots = NULL;

  // (label) & (goto)
  interpreter->pending_jump_index = EXPR_NONE;
  interpreter->resume_target_index = EXPR_NONE;
  interpreter->current_native_call_expression_index = EXPR_NONE;

  // (depth)
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
