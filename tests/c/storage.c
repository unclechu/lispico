#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"
#include "../../src/expression.h"
#include "../../src/symbol.h"

int main(void) {
  static Expression expression_storage[32] = {0};
  static ExpressionAllocation expression_allocation_storage[16] = {0};
  static Symbol symbol_storage[256] = {0};
  static EvalRuntimeAllocation runtime_allocation_storage[2] = {0};
  static uint32_t deferred_expression_storage[2] = {0};

  InterpreterStorage storage = {
    .expressions = expression_storage,
    .expression_capacity = 32,
    .expression_allocations = expression_allocation_storage,
    .expression_allocation_capacity = 16,
    .symbols = symbol_storage,
    .symbol_capacity = 256,
    .runtime_allocations = runtime_allocation_storage,
    .runtime_allocation_capacity = 2,
    .deferred_expression_indices = deferred_expression_storage,
    .deferred_expression_capacity = 2,
  };

  Interpreter interpreter = {0};

  assert(configure_interpreter_storage(&interpreter, &storage));

  // Verify that the interpreter uses the supplied buffers.
  assert(interpreter.storage.expressions == expression_storage);
  assert(interpreter.storage.expression_allocations == expression_allocation_storage);
  assert(interpreter.storage.symbols == symbol_storage);
  assert(interpreter.storage.runtime_allocations == runtime_allocation_storage);
  assert(interpreter.storage.deferred_expression_indices == deferred_expression_storage);

  char error[ERR_MSG_SIZE] = {'\0'};

  // Parse and evaluate with host-provided storage.
  const char *source = "(+ 1 2 3)";
  assert(parse_source(&interpreter, source, strlen(source), &error));

  EvalOutcome outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  assert(outcome.result.kind == SYM_VARIABLE);
  assert(outcome.result.as.value.type == TYPE_U32);
  assert(outcome.result.as.value.as.u32 == 6);

  assert(interpreter.symbol_count > 0);
  assert(interpreter.symbol_count <= interpreter.storage.symbol_capacity);

  // Check the host-provided runtime allocation capacity.
  source = "(str-copy (str \"a\" \"b\"))";
  assert(parse_source(&interpreter, source, strlen(source), &error));

  outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  assert(interpreter.runtime_allocation_count == 2);

  // The third simultaneous runtime allocation must fail.
  source = "(str-copy (str-copy (str \"a\" \"b\")))";
  assert(parse_source(&interpreter, source, strlen(source), &error));

  outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_FAIL);

  // The third deferred registration must exceed capacity.
  source = "(do (defer 1) (defer 2) (defer 3))";
  assert(parse_source(&interpreter, source, strlen(source), &error));

  outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_FAIL);

  // Parser must respect the configured expression capacity.
  char many_expressions[80];

  for (size_t i = 0; i < sizeof(many_expressions); i += 2) {
    many_expressions[i] = '1';
    many_expressions[i + 1] = ' ';
  }

  assert(!parse_source(&interpreter, many_expressions, sizeof(many_expressions), &error));
  assert(strstr(error, "Expression capacity exceeded") != NULL);
  assert(interpreter.expression_count == 0);

  // Cleanup releases allocated payloads but preserves host-owned tables.
  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);

  assert(interpreter.runtime_allocation_count == 0);
  assert(interpreter.expression_allocation_count == 0);
  assert(interpreter.symbol_count == 0);

  assert(interpreter.storage.expressions == expression_storage);
  assert(interpreter.storage.symbols == symbol_storage);

  puts("Host-provided storage tests passed");
  return 0;
}
