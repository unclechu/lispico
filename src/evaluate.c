#include <stdio.h>

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

/*
  Evaluate an expression.

  Set `settings_cb` and `state` to `NULL` if you don’t need to modify any settings.
  `state` is forwarded to `settings_cb` via `EvalSettingsInterface`
  (see `EvalSettingsInterface::state`).
*/
EvalOutcome evaluate(void (*settings_cb)(EvalSettingsInterface*), void *state) {
  // Reset symbols
  clean_symbols();

  // Wipe leftovers of previous evaluation
  wipe_evaluation_data();

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

  // TODO: remove later (just for testing purposes)
  if (eval_outcome.kind == EVAL_OK) {
    const Symbol *sym = symbol_lookup("argv");
    if (sym->kind == SYM_VARIABLE) {
      eval_outcome.result = (EvalResult) {
        .kind = sym->kind,
        .as.value = sym->as.variable,
      };
    } else if (sym->kind == SYM_ARRAY) {
      eval_outcome.result = (EvalResult) {
        .kind = sym->kind,
        .as.array = sym->as.array,
      };
    }
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

void wipe_evaluation_data() {
  // TODO: cleanup references (such as allocated arrays and strings)
  wipe_memory(&eval_outcome, sizeof(eval_outcome));
}

void report_evaluation_failure(char *fail_msg) {
  eval_outcome.kind = EVAL_FAIL;
  snprintf(eval_outcome.as.fail_msg, sizeof(eval_outcome.as.fail_msg), "%s", fail_msg);
}
