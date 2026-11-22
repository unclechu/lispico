#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"
#include "../../src/expression.h"
#include "../../src/symbol.h"

static void check_siblings(
  const Interpreter *interpreter,
  const uint32_t *indices,
  size_t count
) {
  for (size_t i = 0; i < count; ++i) {
    uint32_t expected_next = i + 1 < count ? indices[i + 1] : EXPR_NONE;
    assert(interpreter->storage.expressions[indices[i]].next == expected_next);
  }
}

static void check_location(
  const Interpreter *interpreter,
  uint32_t index,
  size_t line,
  size_t column
) {
  const ExpressionSourceLocation *location =
    get_expression_source_location(interpreter, index);
  assert(location != NULL);
  assert(location->source_id == 0);
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
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  const char *source =
    "#!/usr/bin/env lispico\n"
    "; comment\n"
    "42 () (+ 1 2 3)\n"
    "(do () (+ (* 2 3) 4))\n"
    "17\n";
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(&interpreter, source, strlen(source), &error));
  assert(interpreter.expression_count == 18);

  // Top-level siblings, including an empty list, remain in source order.
  const uint32_t top[] = {0, 1, 2, 7, 17};
  check_siblings(&interpreter, top, sizeof(top) / sizeof(*top));
  assert(expressions[0].kind == EXPR_U32);
  assert(expressions[1].kind == EXPR_LIST);
  assert(expressions[1].first_child == EXPR_NONE);
  assert(expressions[2].kind == EXPR_LIST);
  assert(expressions[7].kind == EXPR_LIST);

  // Lists and argument lists use the same range-linking operation as the root.
  const uint32_t arguments[] = {3, 4, 5, 6};
  assert(expressions[2].first_child == arguments[0]);
  check_siblings(&interpreter, arguments, sizeof(arguments) / sizeof(*arguments));

  const uint32_t do_children[] = {8, 9, 10};
  assert(expressions[7].first_child == do_children[0]);
  check_siblings(&interpreter, do_children, sizeof(do_children) / sizeof(*do_children));
  assert(expressions[9].kind == EXPR_LIST);
  assert(expressions[9].first_child == EXPR_NONE);

  const uint32_t nested_sum[] = {11, 12, 16};
  assert(expressions[10].first_child == nested_sum[0]);
  check_siblings(&interpreter, nested_sum, sizeof(nested_sum) / sizeof(*nested_sum));

  const uint32_t nested_product[] = {13, 14, 15};
  assert(expressions[12].first_child == nested_product[0]);
  check_siblings(&interpreter, nested_product, sizeof(nested_product) / sizeof(*nested_product));

  check_location(&interpreter, 0, 3, 1);
  check_location(&interpreter, 1, 3, 4);
  check_location(&interpreter, 2, 3, 7);
  check_location(&interpreter, 7, 4, 1);
  check_location(&interpreter, 10, 4, 8);
  check_location(&interpreter, 12, 4, 11);
  check_location(&interpreter, 17, 5, 1);

  // Empty sources produce an empty sequence without any expression nodes.
  source = "; nothing here\n#| or here |#\n";
  assert(parse_source(&interpreter, source, strlen(source), &error));
  assert(interpreter.expression_count == 0);
  assert(get_expression_source_location(&interpreter, 0) == NULL);

  // Invalid nested lists still fail atomically.
  source = "42 (do (+ 1 2)";
  assert(!parse_source(&interpreter, source, strlen(source), &error));
  assert(strstr(error, "Unterminated list") != NULL);
  assert(interpreter.expression_count == 0);
  assert(get_expression_source_location(&interpreter, 0) == NULL);

  // Normal sibling evaluation is unchanged after the parser refactor.
  source = "(+ 1 2 3) (+ (* 2 3) 4)";
  assert(parse_source(&interpreter, source, strlen(source), &error));
  assert(expressions[0].next == 5);
  EvalOutcome outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  assert(outcome.result.kind == SYM_VARIABLE);
  assert(outcome.result.as.value.type == TYPE_U32);
  assert(outcome.result.as.value.as.u32 == 10);

  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);

  // The same expression chain is generated when source mapping is disabled.
  Interpreter without_map = {0};
  InterpreterStorage unmapped_storage = storage;
  unmapped_storage.expression_source_locations = NULL;
  unmapped_storage.expression_source_location_capacity = 0;
  assert(configure_interpreter_storage(&without_map, &unmapped_storage));
  assert(parse_source(&without_map, source, strlen(source), &error));
  assert(expressions[0].next == 5);
  assert(get_expression_source_location(&without_map, 0) == NULL);
  outcome = evaluate(&without_map, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  assert(outcome.result.as.value.as.u32 == 10);

  clean_evaluation(&without_map);
  clean_symbols(&without_map);
  clean_expressions(&without_map);

  puts("Parser expression range tests passed");
  return 0;
}
