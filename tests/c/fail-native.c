#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"

static void check_program(
  Interpreter *interpreter,
  const char *source,
  EvalOutcomeKind expected_kind,
  const char *expected_error
) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, source, strlen(source), &error));

  EvalOutcome outcome = evaluate(interpreter, NULL, NULL);
  assert(outcome.kind == expected_kind);
  if (expected_error != NULL) {
    assert(outcome.kind == EVAL_FAIL);
    assert(strcmp(outcome.as.fail_msg, expected_error) == 0);
  }
  assert(interpreter->local_scope == NULL);
  assert(interpreter->active_local_scope == NULL);
  assert(interpreter->function_call == NULL);

  clean_evaluation(interpreter);
  clean_symbols(interpreter);
  clean_expressions(interpreter);
}

int main(void) {
  static Expression expressions[512] = {0};
  static ExpressionAllocation expression_allocations[256] = {0};
  static Symbol symbols[512] = {0};
  static EvalRuntimeAllocation runtime_allocations[512] = {0};
  static uint32_t deferred_indices[64] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 512,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 256,
    .symbols = symbols,
    .symbol_capacity = 512,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 512,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 64,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // The false branch is never evaluated, so even its message is not built.
  check_program(
    &interpreter,
    "(if false (fail (str \"unexpected\" \" message\"))) "
    "(assert true \"accepted\")",
    EVAL_OK,
    NULL
  );

  check_program(&interpreter, "(fail)", EVAL_FAIL, "(fail) evaluation failed");
  check_program(&interpreter, "(fail \"\")", EVAL_FAIL, "(fail) evaluation failed");
  check_program(
    &interpreter,
    "(fail \"bad input\")",
    EVAL_FAIL,
    "(fail) evaluation failed: bad input"
  );
  check_program(
    &interpreter,
    "(var \"limit\" 4) (fail (str \"maximum \" (type s limit)))",
    EVAL_FAIL,
    "(fail) evaluation failed: maximum 4"
  );
  check_program(
    &interpreter,
    "(fail (str-take \"prefix suffix\" 6))",
    EVAL_FAIL,
    "(fail) evaluation failed: prefix"
  );
  check_program(
    &interpreter,
    "(call \"fail\" \"native\")",
    EVAL_FAIL,
    "(fail) evaluation failed: native"
  );
  check_program(
    &interpreter,
    "(apply \"fail\" (arr \"expanded\"))",
    EVAL_FAIL,
    "(fail) evaluation failed: expanded"
  );
  check_program(
    &interpreter,
    "(fail 7)",
    EVAL_FAIL,
    "(fail) message must be a string"
  );
  check_program(
    &interpreter,
    "(fail \"a\" \"b\")",
    EVAL_FAIL,
    "(fail) expects 0 or 1 arguments"
  );

  // Existing `(assert)` validation and messages are unchanged and still eager.
  check_program(&interpreter, "(assert true)", EVAL_OK, NULL);
  check_program(&interpreter, "(assert false)", EVAL_FAIL, "(assert) assertion failed");
  check_program(
    &interpreter,
    "(assert false \"no\")",
    EVAL_FAIL,
    "(assert) assertion failed: no"
  );
  check_program(
    &interpreter,
    "(assert true 123)",
    EVAL_FAIL,
    "(assert) message must be a string"
  );
  check_program(
    &interpreter,
    "(assert \"bad\")",
    EVAL_FAIL,
    "(assert) condition must be a numeric value"
  );

  puts("Fail native-function tests passed");
  return 0;
}
