#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"

static void check_return(
  Interpreter *interpreter,
  const char *source,
  EvalOutcomeKind expected_kind,
  SymbolKind expected_result_kind,
  uint32_t expected_value
) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, source, strlen(source), &error));

  EvalOutcome outcome = evaluate(interpreter, NULL, NULL);
  assert(outcome.kind == expected_kind);

  if (expected_kind == EVAL_OK) {
    assert(outcome.result.kind == expected_result_kind);
    if (expected_result_kind == SYM_VARIABLE) {
      assert(outcome.result.as.value.type == TYPE_U32);
      assert(outcome.result.as.value.as.u32 == expected_value);
    }
  }

  // Function and sequence scopes must be unwound even on an error.
  assert(interpreter->function_call == NULL);
  assert(interpreter->local_scope == NULL);
  assert(interpreter->active_local_scope == NULL);
  assert(interpreter->current_sequence_depth == 0);
  assert(interpreter->pending_leave_count == 0);

  clean_evaluation(interpreter);
  clean_symbols(interpreter);
  clean_expressions(interpreter);
}

int main(void) {
  static Expression expressions[1024] = {0};
  static ExpressionAllocation expression_allocations[512] = {0};
  static Symbol symbols[512] = {0};
  static EvalRuntimeAllocation runtime_allocations[512] = {0};
  static uint32_t deferred_indices[64] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 1024,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 512,
    .symbols = symbols,
    .symbol_capacity = 512,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 512,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 64,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // Unlike `(leave)`, bare `(return)` must not forward the previous value.
  check_return(
    &interpreter,
    "(fn \"f\" (do 123 (return) 456)) (f)",
    EVAL_OK, SYM_EMPTY, 0
  );
  check_return(
    &interpreter,
    "(fn \"f\" (return)) (f)",
    EVAL_OK, SYM_EMPTY, 0
  );
  check_return(
    &interpreter,
    "(fn \"f\" (return empty)) (f)",
    EVAL_OK, SYM_EMPTY, 0
  );
  check_return(
    &interpreter,
    "(fn \"f\" (return 42)) (f)",
    EVAL_OK, SYM_VARIABLE, 42
  );
  check_return(
    &interpreter,
    "(fn \"f\" (do (do (return 17)) 0)) (f)",
    EVAL_OK, SYM_VARIABLE, 17
  );
  check_return(
    &interpreter,
    "(fn \"f\" (do (+ 1 (return 19)) 0)) (f)",
    EVAL_OK, SYM_VARIABLE, 19
  );
  check_return(
    &interpreter,
    "(fn \"f\" (return (return 7))) (f)",
    EVAL_OK, SYM_VARIABLE, 7
  );
  check_return(
    &interpreter,
    "(fn \"f\" (do (return (fn-return-depth)))) (f)",
    EVAL_OK, SYM_VARIABLE, 2
  );

  // A nested function returns to its caller, not the caller's parent scope.
  check_return(
    &interpreter,
    "(fn \"inner\" (return 11)) "
    "(fn \"outer\" (do (assert (== (inner) 11)) (return 22))) "
    "(do (outer))",
    EVAL_OK, SYM_VARIABLE, 22
  );
  check_return(
    &interpreter,
    "(fn \"factorial\" (args \"n\") "
    "  (if (== n 0) (return 1) (return (* n (factorial (- n 1)))))) "
    "(factorial 5)",
    EVAL_OK, SYM_VARIABLE, 120
  );

  // Deferred expressions execute before unwinding the implicit scope.
  check_return(
    &interpreter,
    "(var \"counter\" 0) "
    "(fn \"f\" (do (defer (set \"counter\" 1)) (return 70))) "
    "(assert (== (f) 70)) counter",
    EVAL_OK, SYM_VARIABLE, 1
  );

  // Invalid use reports an evaluation failure instead of propagating leave.
  check_return(&interpreter, "(return)", EVAL_FAIL, SYM_EMPTY, 0);
  check_return(&interpreter, "(do (return 1))", EVAL_FAIL, SYM_EMPTY, 0);
  check_return(
    &interpreter,
    "(fn \"f\" (return 1 2)) (f)",
    EVAL_FAIL, SYM_EMPTY, 0
  );
  check_return(
    &interpreter,
    "(fn \"f\" (return undefined_variable)) (f)",
    EVAL_FAIL, SYM_EMPTY, 0
  );
  check_return(
    &interpreter,
    "(fn \"f\" (do (defer (return 1)) (return 2))) (f)",
    EVAL_FAIL, SYM_EMPTY, 0
  );

  puts("Function return tests passed");
  return 0;
}
