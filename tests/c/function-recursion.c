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
  const char *failure_message
) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, source, strlen(source), &error));

  EvalOutcome outcome = evaluate(interpreter, NULL, NULL);
  assert(outcome.kind == expected_kind);
  if (failure_message != NULL)
    assert(strcmp(outcome.as.fail_msg, failure_message) == 0);

  // Every recursive activation must have been unwound, even on failures,
  // external jumps, or explicit exits.
  assert(interpreter->local_scope == NULL);
  assert(interpreter->active_local_scope == NULL);
  assert(interpreter->function_call == NULL);
  assert(interpreter->gc_roots == NULL);
  assert(interpreter->current_sequence_depth == 0);

  for (uint32_t i = 0; i < interpreter->storage.symbol_capacity; ++i) {
    const Symbol *symbol = &interpreter->storage.symbols[i];
    if (strncmp(symbol->name, "_LOCAL_", sizeof("_LOCAL_") - 1) != 0) continue;
    assert(symbol->kind == SYM_VARIABLE);
    assert(symbol->as.variable.type == TYPE_VOID);
    assert(!symbol->frozen);
    assert(symbol->local_scope_owner == NULL);
    assert(symbol->next_local == NULL);
  }
}

int main(void) {
  static Expression expressions[1024] = {0};
  static ExpressionAllocation expression_allocations[512] = {0};
  static Symbol symbols[512] = {0};
  static EvalRuntimeAllocation runtime_allocations[512] = {0};
  static uint32_t deferred_indices[128] = {0};

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
    .deferred_expression_capacity = 128,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // A recursive call must preserve the outer activation’s arguments and
  // local values, including mutable strings, arrays, and frozen flags.
  check_program(
    &interpreter,
    "(fn \"recurse\" (args \"n\") (do "
    "  (local \"text\" (str \"text-\" (type s n))) "
    "  (local \"words\" (arr (str \"word-\" (type s n)) \"other\")) "
    "  (freeze \"text\") "
    "  (defer (gc)) "
    "  (if (> n 0) (recurse (- n 1))) "
    "  (assert (== text (str \"text-\" (type s n)))) "
    "  (assert (== words (arr (str \"word-\" (type s n)) \"other\"))) "
    "  (assert (is-frozen \"text\")) "
    "  n)) "
    "(assert (== (recurse 6) 6))",
    EVAL_OK,
    NULL
  );

  // A skipped declaration in the nearest activation cannot hide a local
  // still owned by an older activation of the same `(do)` expression.
  check_program(
    &interpreter,
    "(fn \"recurse\" (args \"n\") (do "
    "  (if (== n 2) (local \"text\" \"outer\")) "
    "  (if (== n 0) (local \"text\" \"inner\")) "
    "  (if (> n 0) (recurse (- n 1))) "
    "  (if (== n 2) (assert (== text \"outer\"))))) "
    "(recurse 2)",
    EVAL_OK,
    NULL
  );

  // A goto outside nested function bodies unwinds every activation and runs
  // deferred expressions before resuming at the existing top-level label.
  check_program(
    &interpreter,
    "(var \"phase\" 0) (var \"defers\" 0) "
    "(fn \"escape\" (args \"n\") (do "
    "  (defer (set \"defers\" (+ defers 1))) "
    "  (if (> n 0) (escape (- n 1))) "
    "  (goto finish))) "
    "(label \"finish\") "
    "(if (== phase 1) (do "
    "  (assert (== defers 4)) "
    "  (exit 0))) "
    "(set \"phase\" 1) "
    "(escape 3) "
    "(assert false)",
    EVAL_EXIT,
    NULL
  );

  // A failed inner call still restores the outer activation before its
  // deferred expressions execute, allowing those defers to read its locals.
  check_program(
    &interpreter,
    "(var \"restored\" false) "
    "(fn \"fail\" (args \"n\") (do "
    "  (local \"text\" (str \"value-\" (type s n))) "
    "  (defer (if (== n 2) "
    "    (set \"restored\" (== text \"value-2\")))) "
    "  (if (> n 0) (fail (- n 1))) "
    "  missing)) "
    "(fail 2)",
    EVAL_FAIL,
    "Undefined variable"
  );
  const Symbol *restored = symbol_lookup(&interpreter, "restored");
  assert(restored != NULL);
  assert(restored->kind == SYM_VARIABLE);
  assert(restored->as.variable.type == TYPE_U32);
  assert(restored->as.variable.as.u32 == 1);

  // An unreachable jump into a completed function body remains an ordinary
  // `(goto)` failure; function definitions are not resumable jump paths.
  check_program(
    &interpreter,
    "(fn \"source\" (do (label \"inside\") 1)) "
    "(source) "
    "(goto inside)",
    EVAL_FAIL,
    "(goto) destination is not reachable through (do)"
  );

  // Deep recursion reports an evaluation failure instead of exhausting the
  // interpreter’s C stack indefinitely.
  check_program(
    &interpreter,
    "(fn \"forever\" (forever)) (forever)",
    EVAL_FAIL,
    "(fn) maximum function call depth exceeded"
  );

  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);

  puts("Recursive function activation and goto tests passed");
  return 0;
}
