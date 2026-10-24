#include <stdio.h>
#include <string.h>

#include "evaluate.h"
#include "symbol-error.h"
#include "stdlib.h"
#include "symbol.h"
#include "wipe-memory.h"

// Current expression evaluation result.
//
// Evaluator must reset this value at the beginning of the program evaluation
// (to prevent stale evaluation state).
//
// When `kind` is either `EVAL_EXIT` or `EVAL_FAIL` the evaluator must stop
// evaluating the program.
//
// When a `NativeFunction` fails it must set `kind` to `EVAL_FAIL` and provide
// `fail_msg`.
static EvalOutcome eval_outcome = {
  .kind = EVAL_OK,
  .result = { .kind = SYM_EMPTY },
};

typedef struct {
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
  Symbol *sym = symbol_define(name, kind);
  if (sym == NULL) {
    extract_error_message_from_symbol_define_error(&ctx->fail_msg, name);
    return NULL;
  } else {
    return sym;
  }
}

static Symbol* symbol_lookup_interface(
  const struct EvalSettingsInterface*,
  const char *name
) {
  return symbol_lookup(name);
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

static bool evaluate_expression(uint32_t current_expression_index, Value *result);

// Evaluate top-level or `(do)` expressions sequence
static bool evaluate_sequence(uint32_t current_expression_index, Value *result) {
  *result = (Value) { .type = TYPE_VOID };

  while (current_expression_index != EXPR_NONE) {
    if (current_expression_index >= expression_count) {
      report_evaluation_failure("Invalid expression index");
      return false;
    }

    if (!evaluate_expression(current_expression_index, result)) {
      return false;
    }

    // Save the result of the most recently completed expression.
    eval_outcome.result = (EvalResult) {
      .kind = SYM_VARIABLE,
      .as.value = *result,
    };

    // Move to the next expression in the sequence.
    current_expression_index = expressions[current_expression_index].next;
  }

  return true;
}

static bool evaluate_expression(uint32_t current_expression_index, Value *result) {
  if (current_expression_index == EXPR_NONE || current_expression_index >= expression_count) {
    report_evaluation_failure("Invalid expression index");
    return false;
  }

  const Expression *expr = &expressions[current_expression_index];

  switch (expr->kind) {
    case EXPR_U32:
      *result = (Value) { .type = TYPE_U32, .as.u32 = expr->as.u32 };
      return true;

    case EXPR_I32:
      *result = (Value) { .type = TYPE_I32, .as.i32 = expr->as.i32 };
      return true;

    case EXPR_F32:
      *result = (Value) { .type = TYPE_F32, .as.f32 = expr->as.f32 };
      return true;

    case EXPR_STRING:
      *result = (Value) {
        .type = TYPE_STRING,
        .as.string = {
          .data = expr->as.text,
          .length = strlen(expr->as.text),
        },
      };
      return true;

    case EXPR_LIST: {
      uint32_t fn_name_index = expr->first_child;

      if (fn_name_index == EXPR_NONE || fn_name_index >= expression_count) {
        report_evaluation_failure("Expression list has no valid function name");
        return false;
      }

      const Expression *fn = &expressions[fn_name_index];

      if (fn->kind != EXPR_NAME) {
        report_evaluation_failure("Expression function name must be a name");
        return false;
      }

      // Special form: evaluate child expressions sequentially.
      if (strcmp(fn->as.text, "do") == 0) {
        return evaluate_sequence(fn->next, result);
      }

      // Resolve the function from the existing global symbol table.
      const Symbol *symbol = symbol_lookup(fn->as.text);

      if (symbol == NULL) {
        report_evaluation_failure("Undefined function");
        return false;
      } else if (symbol->kind != SYM_NATIVE_FUNCTION) {
        report_evaluation_failure("Symbol is not a native function");
        return false;
      }

      // Evaluate all arguments before calling the function.
      Value args[EVAL_ARG_CAPACITY] = {0};
      size_t argc = 0;
      uint32_t arg_index = fn->next;

      while (arg_index != EXPR_NONE) {
        if (arg_index >= expression_count) {
          report_evaluation_failure("Invalid argument expression index");
          return false;
        } else if (argc >= EVAL_ARG_CAPACITY) {
          report_evaluation_failure("Too many function arguments");
          return false;
        }

        uint32_t next_index = expressions[arg_index].next;

        // Evaluate child expressions recursively
        if (!evaluate_expression(arg_index, &args[argc])) {
          return false;
        }

        ++argc;
        arg_index = next_index;
      }

      // Call the function pointer already registered in the symbol table.
      *result = symbol->as.native_function(args, argc);

      // Native functions report failures through the evaluation state.
      return eval_outcome.kind == EVAL_OK;
    }

    default:
      report_evaluation_failure("Unsupported expression kind");
      return false;
  }
}

/*
  Evaluate an expression.

  Set `settings_cb` and `state` to `NULL` if you don’t need to modify any settings.
  `state` is forwarded to `settings_cb` via `EvalSettingsInterface`
  (see `EvalSettingsInterface::state`).
*/
EvalOutcome evaluate(
  const Expression *eval_expressions,
  uint32_t eval_expression_count,
  void (*settings_cb)(EvalSettingsInterface*),
  void *state
) {
  // Reset symbols
  clean_symbols();

  // Wipe leftovers of previous evaluation
  clean_evaluation();

  // Wipe global expressions table if expressions are coming from somewhere else.
  // TODO: Copy `eval_expressions` into the global `expressions`.
  if (eval_expressions != expressions) {
    clean_expressions();
  }

  expression_count = eval_expression_count;

  // Reset evaluation state
  eval_outcome = (EvalOutcome) {
    .kind = EVAL_OK,
    .result = { .kind = SYM_EMPTY },
  };

  EvalSettingsInterfaceContext settings_ctx = {
    .is_failed = false,
    .fail_msg = {'\0'},
  };

  char fail_prefix[64] = {'\0'};

  snprintf(settings_ctx.fail_msg, sizeof(settings_ctx.fail_msg), "populate_std failed: ");
  if (!populate_stdlib(&settings_ctx.fail_msg)) goto fail;

  EvalSettingsInterface settings_interface = {
    .context = &settings_ctx,
    .state = state,
    .define_symbol = define_symbol_interface,
    .symbol_lookup = symbol_lookup_interface,
    .report_failure = report_failure_interface,
  };

  if (settings_cb != NULL) {
    snprintf(settings_ctx.fail_msg, sizeof(settings_ctx.fail_msg), "EvalSettingsCallback failed: ");
    settings_cb(&settings_interface);
    if (settings_ctx.is_failed) goto fail;
  }

  if (expression_count != 0) {
    Value result_value = { .type = TYPE_VOID };

    // Evaluate all top-level expressions sequentially.
    // Discard returned boolean, the result is already reported to `eval_outcome`.
    (void)evaluate_sequence(0, &result_value);

    wipe_memory(&result_value, sizeof(result_value));
  }

  return eval_outcome;

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

void clean_evaluation() {
  // TODO: cleanup references (such as allocated arrays and strings)
  wipe_memory(&eval_outcome, sizeof(eval_outcome));
}

void report_evaluation_failure(char *fail_msg) {
  eval_outcome.kind = EVAL_FAIL;
  snprintf(eval_outcome.as.fail_msg, sizeof(eval_outcome.as.fail_msg), "%s", fail_msg);
}

bool get_last_evaluation_value(Value *value) {
  if (eval_outcome.result.kind != SYM_VARIABLE) return false;
  *value = eval_outcome.result.as.value;
  return true;
}
