#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"
#include "../../src/expression.h"

static void check_location(
  const Interpreter *interpreter,
  uint32_t index,
  size_t byte_offset,
  size_t line,
  size_t column
) {
  const ExpressionSourceLocation *location =
    get_expression_source_location(interpreter, index);

  assert(location != NULL);
  assert(location->byte_offset == byte_offset);
  assert(location->line == line);
  assert(location->column == column);
}

int main(void) {
  static Expression expressions[64] = {0};
  static ExpressionSourceLocation locations[64] = {0};
  static ExpressionAllocation expression_allocations[32] = {0};
  static Symbol symbols[256] = {0};
  static EvalRuntimeAllocation runtime_allocations[32] = {0};
  static uint32_t deferred_expressions[16] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 64,
    .expression_source_locations = locations,
    .expression_source_location_capacity = 64,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 32,
    .symbols = symbols,
    .symbol_capacity = 256,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 32,
    .deferred_expression_indices = deferred_expressions,
    .deferred_expression_capacity = 16,
  };

  // A supplied table must cover every expression, even when it is unused.
  Interpreter invalid = {0};
  InterpreterStorage too_small = storage;
  too_small.expression_source_location_capacity = 63;
  assert(!configure_interpreter_storage(&invalid, &too_small));

  InterpreterStorage missing = storage;
  missing.expression_source_locations = NULL;
  assert(!configure_interpreter_storage(&invalid, &missing));

  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));
  assert(get_expression_source_location(&interpreter, 0) == NULL);
  assert(get_expression_source_location(&interpreter, EXPR_NONE) == NULL);

  const char *source =
    "#!/usr/bin/env lispico\n"
    "; A line comment\n"
    "  (do\n"
    "    (+ 1\n"
    "       missing) #| an outer comment\n"
    "  with #| nested |# text |#\n"
    "    (println \"hello\\n\"))\n";
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(&interpreter, source, strlen(source), &error));
  assert(interpreter.expression_count == 9);

  // Every expression maps to its opening character, not its closing one.
  check_location(&interpreter, 0, (size_t)(strstr(source, "(do") - source), 3, 3);
  check_location(&interpreter, 1, (size_t)(strstr(source, "do") - source), 3, 4);
  check_location(&interpreter, 2, (size_t)(strstr(source, "(+") - source), 4, 5);
  check_location(&interpreter, 3, (size_t)(strstr(source, "+ 1") - source), 4, 6);
  check_location(&interpreter, 4, (size_t)(strstr(source, "1\n") - source), 4, 8);
  check_location(&interpreter, 5, (size_t)(strstr(source, "missing") - source), 5, 8);
  check_location(&interpreter, 6, (size_t)(strstr(source, "(println") - source), 7, 5);
  check_location(&interpreter, 7, (size_t)(strstr(source, "println") - source), 7, 6);
  check_location(&interpreter, 8, (size_t)(strstr(source, "\"hello") - source), 7, 14);

  EvalOutcome outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_FAIL);
  assert(outcome.failure_expression_index == 5);
  assert(strstr(outcome.as.fail_msg, "Undefined variable") != NULL);
  check_location(
    &interpreter,
    outcome.failure_expression_index,
    (size_t)(strstr(source, "missing") - source),
    5,
    8
  );

  // A subsequent parse must replace all prior location entries.
  source = "123";
  assert(parse_source(&interpreter, source, strlen(source), &error));
  check_location(&interpreter, 0, 0, 1, 1);
  assert(get_expression_source_location(&interpreter, 5) == NULL);
  for (uint32_t i = 1; i < 9; ++i)
    assert(locations[i].line == 0 && locations[i].column == 0);

  // A parsing failure must clear both expressions and source locations.
  source = "(+ 1 (";
  assert(!parse_source(&interpreter, source, strlen(source), &error));
  assert(interpreter.expression_count == 0);
  assert(get_expression_source_location(&interpreter, 0) == NULL);
  for (uint32_t i = 0; i < 9; ++i)
    assert(locations[i].line == 0 && locations[i].column == 0);

  // With no map, expression indices and evaluation remain available.
  Interpreter without_map = {0};
  InterpreterStorage unmapped_storage = storage;
  unmapped_storage.expression_source_locations = NULL;
  unmapped_storage.expression_source_location_capacity = 0;
  assert(configure_interpreter_storage(&without_map, &unmapped_storage));
  source = "(+ 1 missing)";
  assert(parse_source(&without_map, source, strlen(source), &error));
  assert(get_expression_source_location(&without_map, 0) == NULL);
  outcome = evaluate(&without_map, NULL, NULL);
  assert(outcome.kind == EVAL_FAIL);
  assert(outcome.failure_expression_index == 3);
  assert(get_expression_source_location(&without_map, outcome.failure_expression_index) == NULL);

  clean_evaluation(&without_map);
  clean_symbols(&without_map);
  clean_expressions(&without_map);
  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);

  puts("Expression source map tests passed");
  return 0;
}
