#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"

static void check_local_cleanup(
  Interpreter *interpreter,
  const char *source,
  EvalOutcomeKind expected_outcome
) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, source, strlen(source), &error));

  EvalOutcome outcome = evaluate(interpreter, NULL, NULL);
  assert(outcome.kind == expected_outcome);

  // The (do) is expression 0, and its symbol remains in the hash table.
  const Symbol *symbol = symbol_lookup(interpreter, "_LOCAL_0_x");
  assert(symbol != NULL);
  assert(symbol->kind == SYM_VARIABLE);
  assert(symbol->as.variable.type == TYPE_VOID);
  assert(!symbol->frozen);
  assert(interpreter->local_scope == NULL);
}

int main(void) {
  static Expression expressions[256] = {0};
  static ExpressionAllocation expression_allocations[128] = {0};
  static Symbol symbols[256] = {0};
  static EvalRuntimeAllocation runtime_allocations[128] = {0};
  static uint32_t deferred_indices[32] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 256,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 128,
    .symbols = symbols,
    .symbol_capacity = 256,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 128,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 32,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // Successful cleanup unfreezes a local and resets its actual type.
  check_local_cleanup(
    &interpreter,
    "(do (local \"x\" \"hello\") (freeze \"x\"))",
    EVAL_OK
  );

  // Cleanup also runs when an expression inside the scope fails.
  check_local_cleanup(
    &interpreter,
    "(do (local \"x\" (arr 1 2)) (freeze \"x\") missing)",
    EVAL_FAIL
  );

  // Cleanup happens after deferred expressions, even on (leave).
  check_local_cleanup(
    &interpreter,
    "(do (local \"x\" 123) (defer (assert (== x 123))) (leave))",
    EVAL_OK
  );

  // An explicit exit also unwinds the scope and resets frozen locals.
  check_local_cleanup(
    &interpreter,
    "(do (local \"x\" \"text\") (freeze \"x\") (exit 0))",
    EVAL_EXIT
  );

  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);

  puts("Pseudo-local scope cleanup tests passed");
  return 0;
}
