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

  Interpreter *interpreter;

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

// Evaluate expressions from the configured interpreter storage.
EvalOutcome evaluate(
  Interpreter *interpreter,
  void (*settings_cb)(EvalSettingsInterface*),
  void *state
);

void clean_evaluation(Interpreter *interpreter);

// Memory is tracked by the evaluator and freed by `clean_evaluation`.
// The caller must not free the returned pointer.
void *allocate_runtime_data(Interpreter *interpreter, size_t size);

// Transfer a malloc/realloc-allocated buffer to the evaluator for cleanup.
// `size` is the allocated capacity in bytes, not necessarily the value length.
// The caller retains ownership if registration fails.
bool adopt_runtime_data(Interpreter *interpreter, void *data, size_t size);
