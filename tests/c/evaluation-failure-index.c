#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"

static void check_failure(
  Interpreter *interpreter,
  const char *source,
  uint32_t expected_index,
  const char *expected_message
) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, source, strlen(source), &error));

  EvalOutcome outcome = evaluate(interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_FAIL);
  assert(outcome.failure_expression_index == expected_index);
  assert(strstr(outcome.as.fail_msg, expected_message) != NULL);
}

static void report_settings_failure(EvalSettingsInterface *interface) {
  interface->report_failure(interface, "Test settings failure");
}

int main(void) {
  static Expression expression_storage[256] = {0};
  static ExpressionAllocation expression_allocation_storage[128] = {0};
  static Symbol symbol_storage[256] = {0};
  static EvalRuntimeAllocation runtime_allocation_storage[64] = {0};
  static uint32_t deferred_expression_storage[32] = {0};

  InterpreterStorage storage = {
    .expressions = expression_storage,
    .expression_capacity = 256,
    .expression_allocations = expression_allocation_storage,
    .expression_allocation_capacity = 128,
    .symbols = symbol_storage,
    .symbol_capacity = 256,
    .runtime_allocations = runtime_allocation_storage,
    .runtime_allocation_capacity = 64,
    .deferred_expression_indices = deferred_expression_storage,
    .deferred_expression_capacity = 32,
  };

  // Failures before expression evaluation have no expression index.
  Interpreter unconfigured = {0};
  EvalOutcome outcome = evaluate(&unconfigured, NULL, NULL);
  assert(outcome.kind == EVAL_FAIL);
  assert(outcome.failure_expression_index == EXPR_NONE);

  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  outcome = evaluate(&interpreter, report_settings_failure, NULL);
  assert(outcome.kind == EVAL_FAIL);
  assert(outcome.failure_expression_index == EXPR_NONE);

  // Failures point to the innermost evaluated expression, not its parent.
  check_failure(&interpreter, "missing", 0, "Undefined variable");
  check_failure(&interpreter, "(+ 1 missing)", 3, "Undefined variable");
  check_failure(&interpreter, "(+ 1 (unknown 2))", 3, "Undefined function");
  check_failure(&interpreter, "(if 1 (+ 1 missing) 0)", 6, "Undefined variable");

  // Type checks performed by a special form belong to the form itself.
  check_failure(&interpreter, "(if \"text\" 1 2)", 0, "condition must be a numeric value");

  // Native function failures use the native call’s expression index.
  check_failure(&interpreter, "(+ 1 (arr-get \"none\" 3))", 3, "not defined");

  // Deferred failures retain their own location across state restoration.
  check_failure(&interpreter, "(do (defer missing) 123)", 4, "Undefined variable");
  check_failure(
    &interpreter,
    "(do (defer (exit 7)) 123)",
    4,
    "deferred expression cannot interrupt"
  );

  // An existing sequence failure takes priority over a later deferred failure.
  check_failure(&interpreter, "(do (defer missing) (unknown))", 5, "Undefined function");

  // An unreachable label points to the (goto) call, not the enclosing sequence.
  check_failure(
    &interpreter,
    "(show (label \"hidden\")) (goto hidden)",
    5,
    "destination is not reachable"
  );

  // Evaluation of the next program must not inherit a previous failure index.
  char error[ERR_MSG_SIZE] = {'\0'};
  const char *source = "(+ 1 2)";
  assert(parse_source(&interpreter, source, strlen(source), &error));
  outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  assert(outcome.failure_expression_index == EXPR_NONE);

  source = "(exit 7)";
  assert(parse_source(&interpreter, source, strlen(source), &error));
  outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_EXIT);
  assert(outcome.as.exit_code == 7);
  assert(outcome.failure_expression_index == EXPR_NONE);

  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);

  puts("Evaluation failure expression index tests passed");
  return 0;
}
