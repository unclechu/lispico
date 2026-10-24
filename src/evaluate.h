#pragma once

#include "symbol.h"
#include "expression.h"

typedef enum {
  EVAL_OK = 0,
  EVAL_EXIT,
  EVAL_FAIL,
} EvalOutcomeKind;

typedef struct {
  SymbolKind kind; // Only one of these: SYM_EMPTY, SYM_VARIABLE, SYM_ARRAY
  union {
    Value value; // `kind == SYM_VARIABLE`
    Array array; // `kind == SYM_ARRAY`
  } as;
} EvalResult;

typedef struct {
  EvalOutcomeKind kind;
  EvalResult result; // Result of expression evaluation
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

  Symbol* (*symbol_lookup)(
    const struct EvalSettingsInterface *self,
    const char *name
  );

  void (*report_failure)(
    const struct EvalSettingsInterface *self,
    const char *err_msg
  );
} EvalSettingsInterface;

EvalOutcome evaluate(
  const Expression *eval_expressions,
  uint32_t eval_expression_count,
  void (*settings_cb)(EvalSettingsInterface*),
  void *state
);

void report_evaluation_failure(char *fail_msg);
void clean_evaluation();
