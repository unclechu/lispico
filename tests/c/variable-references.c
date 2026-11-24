#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"
#include "../../src/stdlib-variables.h"

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
  static Expression expressions[1024] = {0};
  static ExpressionAllocation expression_allocations[512] = {0};
  static Symbol symbols[512] = {0};
  static EvalRuntimeAllocation runtime_allocations[1024] = {0};
  static uint32_t deferred_indices[64] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 1024,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 512,
    .symbols = symbols,
    .symbol_capacity = 512,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 1024,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 64,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // The result borrows the symbol’s name without a runtime allocation.
  Symbol *global = symbol_define(&interpreter, "value", SYM_VARIABLE);
  assert(global != NULL);
  global->as.variable = (Value) { .type = TYPE_U32, .as.u32 = 10 };
  const char *name = "value";
  Datum arg = {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_STRING,
      .as.string = { .data = name, .base = name, .length = 5, .nul_terminated = true },
    },
  };
  Datum reference = std_var_ref(&interpreter, &arg, 1);
  assert(reference.kind == SYM_VARIABLE);
  assert(reference.as.value.type == TYPE_STRING);
  assert(reference.as.value.as.string.length == 5);
  assert(strcmp(reference.as.value.as.string.data, "value") == 0);
  assert(reference.as.value.as.string.data == global->name);
  assert(reference.as.value.as.string.base == global->name);
  assert(reference.as.value.as.string.immutable_ref);
  assert(reference.as.value.as.string.nul_terminated);
  assert(interpreter.runtime_allocation_count == 0);
  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);

  // Passing stored names accesses caller-owned values, including shadowed
  // globals, without copying the string or array contents into arguments.
  check_program(
    &interpreter,
    "(fn \"change\" (args \"target\") (str-set target 0 (str-char \"x\"))) "
    "(fn \"change-arr\" (args \"target\") (arr-set target 0 7)) "
    "(var \"message\" \"hi\") "
    "(assert (== (var-ref \"message\") \"message\")) "
    "(assert (is-immutable-ref (var-ref \"message\"))) "
    "(assert (== (has-immutable-refs (copy (var-ref \"message\"))) false)) "
    "(change (var-ref \"message\")) "
    "(assert (== message \"xi\")) "
    "(do (local \"message\" \"yo\") "
    "  (local-arr-new \"items\" u32 2) "
    "  (local \"ref\" (var-ref \"message\")) "
    "  (assert (!= ref \"message\")) "
    "  (change ref) "
    "  (assert (== message \"xo\")) "
    "  (change-arr (var-ref \"items\")) "
    "  (assert (== (arr-get \"items\" 0) 7)) "
    "  (do (local \"message\" \"ab\") "
    "    (assert (!= ref (var-ref \"message\"))) "
    "    (change (var-ref \"message\")) "
    "    (assert (== message \"xb\"))) "
    "  (assert (== message \"xo\"))) "
    "(assert (== message \"xi\"))",
    EVAL_OK,
    NULL
  );

  // A recursive re-entry uses the same stored local name for its own value.
  // After unwinding, the name resolves to the restored outer activation.
  // After both scopes exit, the stale name identifies a reset TYPE_VOID slot.
  check_program(
    &interpreter,
    "(var \"saved\" \"\") "
    "(fn \"walk\" (args \"n\") (do "
    "  (local \"item\" (+ n 10)) "
    "  (if (== n 1) (do "
    "    (override \"saved\" (var-ref \"item\")) "
    "    (assert (== (get saved) 11)) "
    "    (walk 0) "
    "    (assert (== (get saved) 11)))) "
    "  (if (== n 0) (do "
    "    (assert (== (get saved) 10)) "
    "    (set saved 13) "
    "    (assert (== item 13)))))) "
    "(walk 1) "
    "(assert (== (type-of (get saved)) void))",
    EVAL_OK,
    NULL
  );

  // TYPE_VOID and frozen variables can still be referenced.
  check_program(
    &interpreter,
    "(var \"x\" 7) (freeze \"x\") "
    "(assert (== (var-ref \"x\") \"x\")) "
    "(var \"gone\" 3) (reset \"gone\") "
    "(assert (== (var-ref \"gone\") \"gone\"))",
    EVAL_OK,
    NULL
  );

  // A user-defined function does not mask an ordinary global variable.
  check_program(
    &interpreter,
    "(fn \"foo\" 1) (var \"foo\" 9) "
    "(assert (== (var-ref \"foo\") \"foo\")) "
    "(assert (== (foo) 1))",
    EVAL_OK,
    NULL
  );

  // Name views cannot be used to write into symbol-table key storage.
  check_program(
    &interpreter,
    "(var \"target\" 1) (var \"name\" (var-ref \"target\")) "
    "(str-set \"name\" 0 97)",
    EVAL_FAIL,
    "(str-set) cannot modify an immutable string reference"
  );
  check_program(
    &interpreter,
    "(var \"name\" (arr-get (variables) 0)) "
    "(str-set \"name\" 0 97)",
    EVAL_FAIL,
    "(str-set) cannot modify an immutable string reference"
  );

  check_program(
    &interpreter,
    "(fn \"foo\" 1) (var-ref \"foo\")",
    EVAL_FAIL,
    "(var-ref) variable “foo” is not defined"
  );
  check_program(
    &interpreter,
    "(label \"here\") (var-ref \"here\")",
    EVAL_FAIL,
    "(var-ref) symbol “here” is not a variable or array"
  );
  check_program(
    &interpreter,
    "(var-ref \"+\")",
    EVAL_FAIL,
    "(var-ref) symbol “+” is not a variable or array"
  );
  check_program(
    &interpreter,
    "(var-ref \"missing\")",
    EVAL_FAIL,
    "(var-ref) variable “missing” is not defined"
  );
  check_program(
    &interpreter,
    "(var-ref 7)",
    EVAL_FAIL,
    "(var-ref) invalid variable name"
  );
  check_program(
    &interpreter,
    "(var-ref)",
    EVAL_FAIL,
    "(var-ref) expects exactly 1 argument, got instead: 0"
  );
  check_program(
    &interpreter,
    "(var-ref \"x\" \"y\")",
    EVAL_FAIL,
    "(var-ref) expects exactly 1 argument, got instead: 2"
  );

  puts("Variable reference tests passed");
  return 0;
}
