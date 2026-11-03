#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"
#include "../../src/symbol.h"

typedef struct {
  Expression expressions[128];
  ExpressionAllocation expression_allocations[64];
  Symbol symbols[256];
  EvalRuntimeAllocation runtime_allocations[16];
  uint32_t deferred_expression_indices[16];
} TestStorage;

static bool configure_test_interpreter(Interpreter *interpreter, TestStorage *buffers) {
  InterpreterStorage storage = {
    .expressions = buffers->expressions,
    .expression_capacity = 128,
    .expression_allocations = buffers->expression_allocations,
    .expression_allocation_capacity = 64,
    .symbols = buffers->symbols,
    .symbol_capacity = 256,
    .runtime_allocations = buffers->runtime_allocations,
    .runtime_allocation_capacity = 16,
    .deferred_expression_indices = buffers->deferred_expression_indices,
    .deferred_expression_capacity = 16,
  };

  return configure_interpreter_storage(interpreter, &storage);
}

int main(void) {
  static TestStorage buffers_a = {0};
  static TestStorage buffers_b = {0};
  Interpreter first = {0};
  Interpreter second = {0};
  char error[ERR_MSG_SIZE] = {'\0'};

  assert(configure_test_interpreter(&first, &buffers_a));
  assert(configure_test_interpreter(&second, &buffers_b));

  // Both interpreters must use separate host-provided storage.
  assert(first.storage.expressions != second.storage.expressions);
  assert(first.storage.expression_allocations != second.storage.expression_allocations);
  assert(first.storage.symbols != second.storage.symbols);
  assert(first.storage.runtime_allocations != second.storage.runtime_allocations);
  assert(first.storage.deferred_expression_indices != second.storage.deferred_expression_indices);

  const char *source_a = "(var \"only_a\" 123) only_a";
  const char *source_b = "(var \"only_b\" 456) only_b";

  // Parse both programs before evaluating either one.
  assert(parse_source(&first, source_a, strlen(source_a), &error));
  assert(parse_source(&second, source_b, strlen(source_b), &error));
  assert(first.expression_count > 0);
  assert(second.expression_count > 0);

  EvalOutcome result_a = evaluate(&first, NULL, NULL);
  EvalOutcome result_b = evaluate(&second, NULL, NULL);

  assert(result_a.kind == EVAL_OK);
  assert(result_b.kind == EVAL_OK);
  assert(result_a.result.kind == SYM_VARIABLE);
  assert(result_b.result.kind == SYM_VARIABLE);
  assert(result_a.result.as.value.type == TYPE_U32);
  assert(result_b.result.as.value.type == TYPE_U32);
  assert(result_a.result.as.value.as.u32 == 123);
  assert(result_b.result.as.value.as.u32 == 456);

  // Variables must not leak between instances.
  assert(symbol_lookup(&first, "only_a") != NULL);
  assert(symbol_lookup(&first, "only_b") == NULL);
  assert(symbol_lookup(&second, "only_b") != NULL);
  assert(symbol_lookup(&second, "only_a") == NULL);

  // Cleanup of one instance must not affect the other.
  clean_evaluation(&first);
  clean_symbols(&first);
  clean_expressions(&first);

  assert(first.expression_count == 0);
  assert(first.symbol_count == 0);
  assert(first.runtime_allocation_count == 0);
  assert(second.expression_count > 0);
  assert(symbol_lookup(&second, "only_b") != NULL);
  assert(result_b.result.as.value.as.u32 == 456);

  // Allocate independent string data in both interpreters.
  source_a = "(var \"text_a\" (str-copy \"ABCDEF\" 3)) text_a";
  source_b = "(var \"text_b\" (str-copy \"abcdef\" 3)) text_b";
  assert(parse_source(&first, source_a, strlen(source_a), &error));
  assert(parse_source(&second, source_b, strlen(source_b), &error));

  result_a = evaluate(&first, NULL, NULL);
  result_b = evaluate(&second, NULL, NULL);
  assert(result_a.kind == EVAL_OK);
  assert(result_b.kind == EVAL_OK);
  assert(result_a.result.as.value.type == TYPE_STRING);
  assert(result_b.result.as.value.type == TYPE_STRING);
  assert(result_a.result.as.value.as.string.length == 3);
  assert(result_b.result.as.value.as.string.length == 3);
  assert(result_a.result.as.value.as.string.data != NULL);
  assert(result_b.result.as.value.as.string.data != NULL);
  assert(memcmp(result_a.result.as.value.as.string.data, "ABC", 3) == 0);
  assert(memcmp(result_b.result.as.value.as.string.data, "abc", 3) == 0);
  assert(result_a.result.as.value.as.string.data != result_b.result.as.value.as.string.data);

  uint32_t second_allocation_count = second.runtime_allocation_count;
  assert(second_allocation_count > 0);

  // Releasing A's string allocations must leave B's string valid.
  clean_evaluation(&first);
  clean_symbols(&first);
  clean_expressions(&first);
  assert(second.runtime_allocation_count == second_allocation_count);
  assert(symbol_lookup(&second, "text_b") != NULL);
  assert(memcmp(result_b.result.as.value.as.string.data, "abc", 3) == 0);

  // An evaluation failure in A must not change B's evaluation state.
  source_a = "undefined_name";
  assert(parse_source(&first, source_a, strlen(source_a), &error));
  result_a = evaluate(&first, NULL, NULL);
  assert(result_a.kind == EVAL_FAIL);
  assert(second.eval_outcome.kind == EVAL_OK);
  assert(symbol_lookup(&second, "text_b") != NULL);
  assert(memcmp(result_b.result.as.value.as.string.data, "abc", 3) == 0);

  clean_evaluation(&first);
  clean_symbols(&first);
  clean_expressions(&first);
  clean_evaluation(&second);
  clean_symbols(&second);
  clean_expressions(&second);

  assert(first.runtime_allocation_count == 0);
  assert(second.runtime_allocation_count == 0);
  assert(first.symbol_count == 0);
  assert(second.symbol_count == 0);

  puts("Interpreter isolation tests passed");
  return 0;
}
