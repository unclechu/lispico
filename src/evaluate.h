#pragma once

#include "symbol.h"
#include "expression.h"

typedef enum {
  EVAL_OK = 0,
  EVAL_EXIT,
  EVAL_FAIL,
} EvalOutcomeKind;

typedef struct {
  EvalOutcomeKind kind;
  Datum result; // Result of expression evaluation
  union {
    // `kind == EVAL_EXIT`
    int exit_code;
    // `kind == EVAL_FAIL`
    char fail_msg[ERR_MSG_SIZE];
  } as;
} EvalOutcome;

typedef struct EvalSettingsInterface {
  void *context; // Internal context
  void *state;

  // Caller is expect to stop settings callback evaluation if `NULL` is returned
  // (it indicates a failure, which is automatically forwarded to `EvalOutcome`).
  Symbol* (*define_symbol)(
    const struct EvalSettingsInterface *self,
    const char *name,
    SymbolKind kind
  );

  // Try to find a symbol by its name.
  // Returns `NULL` is symbol with specified name is not found in the symbols table.
  Symbol* (*symbol_lookup)(
    const struct EvalSettingsInterface *self,
    const char *name
  );

  // Report evaluation failure.
  // After such report evaluation will stop and resulting `EvalOutcome`
  // will contain the failure state.
  void (*report_failure)(
    const struct EvalSettingsInterface *self,
    const char *err_msg
  );
} EvalSettingsInterface;

EvalOutcome evaluate(
  // Optionally custom expressions array.
  // If you want to use global fixed-memory expressions array just set:
  // `eval_expressions = expressions` and
  // `eval_expression_count = expression_count`.
  const Expression *eval_expressions,
  uint32_t eval_expression_count,

  void (*settings_cb)(EvalSettingsInterface*),
  void *state
);

void report_evaluation_failure(char *fail_msg);
void clean_evaluation();

bool get_last_evaluation_value(Datum *result);

void report_evaluation_exit(int exit_code, Datum result);
