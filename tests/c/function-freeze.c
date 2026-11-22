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
  const char *failure_message,
  const char *function_name,
  bool expected_frozen
) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, source, strlen(source), &error));

  EvalOutcome outcome = evaluate(interpreter, NULL, NULL);
  assert(outcome.kind == expected_kind);
  if (failure_message != NULL)
    assert(strcmp(outcome.as.fail_msg, failure_message) == 0);

  if (function_name != NULL) {
    char label_name[SYMBOL_NAME_SIZE] = {'\0'};
    int length = snprintf(
      label_name,
      sizeof(label_name),
      EVAL_FN_LABEL_FORMAT_TEMPLATE,
      function_name
    );
    assert(length > 0 && (size_t)length < sizeof(label_name));
    const Symbol *function = symbol_lookup(interpreter, label_name);
    assert(function != NULL);
    assert(function->kind == SYM_LABEL);
    assert(function->frozen == expected_frozen);
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
  static EvalRuntimeAllocation runtime_allocations[256] = {0};
  static uint32_t deferred_indices[64] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 512,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 256,
    .symbols = symbols,
    .symbol_capacity = 512,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 256,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 64,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // An ordinary function remains redefinable until it is frozen.
  check_program(
    &interpreter,
    "(fn \"foo\" 1) (fn \"foo\" 2) "
    "(assert (== (foo) 2)) "
    "(assert (== (is-frozen \"foo\") false))",
    EVAL_OK, NULL, "foo", false
  );

  // Public function names resolve to the `_FN_` symbol for freezing.
  check_program(
    &interpreter,
    "(fn \"foo\" 1) "
    "(freeze \"foo\") (freeze \"foo\") "
    "(assert (is-frozen \"foo\")) "
    "(assert (is-frozen \"_FN_foo\")) "
    "(assert (== (foo) 1)) (assert (== (call \"foo\") 1))",
    EVAL_OK, NULL, "foo", true
  );

  check_program(
    &interpreter,
    "(fn \"foo\" 1) (freeze \"foo\") (fn \"foo\" 2)",
    EVAL_FAIL, "(fn) cannot redefine a frozen label", "foo", true
  );

  // The generated label can still be referenced by its exact stored name.
  check_program(
    &interpreter,
    "(fn \"foo\" 1) (freeze \"_FN_foo\") "
    "(assert (is-frozen \"foo\"))",
    EVAL_OK, NULL, "foo", true
  );

  // A frozen definition can be encountered again at the same expression.
  check_program(
    &interpreter,
    "(var \"visits\" 0) (label \"again\") "
    "(fn \"foo\" 1) "
    "(if (== visits 0) (do "
    "  (freeze \"foo\") "
    "  (set \"visits\" 1) "
    "  (goto again))) "
    "(assert (is-frozen \"foo\"))",
    EVAL_OK, NULL, "foo", true
  );

  // Dispatch chooses a user function over an equally named native function.
  check_program(
    &interpreter,
    "(fn \"println\" 7) (freeze \"println\") "
    "(assert (is-frozen \"_FN_println\")) "
    "(assert (== (println) 7))",
    EVAL_OK, NULL, "println", true
  );

  // A visible local name still identifies the local symbol first.
  check_program(
    &interpreter,
    "(fn \"foo\" 1) "
    "(do (local \"foo\" 2) (freeze \"foo\") "
    "  (assert (is-frozen \"foo\"))) "
    "(assert (== (is-frozen \"foo\") false))",
    EVAL_OK, NULL, "foo", false
  );

  // Ordinary globals and native functions retain their existing behavior.
  check_program(
    &interpreter,
    "(var \"foo\" 3) (freeze \"foo\") "
    "(assert (is-frozen \"foo\")) "
    "(freeze \"assert\") (assert (is-frozen \"assert\"))",
    EVAL_OK, NULL, NULL, false
  );

  check_program(
    &interpreter,
    "(freeze \"missing\")",
    EVAL_FAIL, "(freeze) symbol “missing” is not defined", NULL, false
  );

  puts("User-defined function freezing tests passed");
  return 0;
}
