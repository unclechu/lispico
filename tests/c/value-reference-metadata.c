#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"
#include "../../src/evaluate-copy.h"
#include "../../src/symbol.h"

static void check_string(String string, const char *contents, size_t length) {
  assert(string.data != NULL);
  assert(string.base == string.data);
  assert(string.length == length);
  assert(!string.immutable_ref);
  assert(string.nul_terminated);
  assert(memcmp(string.data, contents, length) == 0);
  assert(string.data[length] == '\0');
}

static void check_array(Array array, ValueType type, uint32_t length) {
  assert(array.element_type == type);
  assert(array.length == length);
  assert(array.data != NULL);
  assert(array.base == array.data);
  assert(!array.immutable_ref);
}

int main(void) {
  static Expression expressions[256] = {0};
  static ExpressionAllocation expression_allocations[256] = {0};
  static Symbol symbols[512] = {0};
  static EvalRuntimeAllocation runtime_allocations[512] = {0};
  static uint32_t deferred_indices[32] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 256,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 256,
    .symbols = symbols,
    .symbol_capacity = 512,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 512,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 32,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // An expression-backed string preserves its independent backing pointer.
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(&interpreter, "\"hello\"", 7, &error));
  EvalOutcome outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  assert(outcome.result.kind == SYM_VARIABLE);
  check_string(outcome.result.as.value.as.string, "hello", 5);
  assert(outcome.result.as.value.as.string.data == expression_allocations[0].data);
  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);

  // Mutable variables, string arrays, array elements, and string slices still
  // contain independently allocated, NUL-terminated strings in this stage.
  const char *program =
    "(var \"text\" \"hello\") "
    "(var \"words\" (arr \"first\" \"second\")) "
    "(var \"cut\" (str-slice text 1 3)) "
    "(arr-get \"words\" 1)";
  assert(parse_source(&interpreter, program, strlen(program), &error));
  outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  check_string(outcome.result.as.value.as.string, "second", 6);

  const Symbol *text = symbol_lookup(&interpreter, "text");
  const Symbol *cut = symbol_lookup(&interpreter, "cut");
  const Symbol *words = symbol_lookup(&interpreter, "words");
  assert(text != NULL && cut != NULL && words != NULL);
  check_string(text->as.variable.as.string, "hello", 5);
  check_string(cut->as.variable.as.string, "ell", 3);
  check_array(words->as.array, TYPE_STRING, 2);
  const String *elements = words->as.array.data;
  check_string(elements[0], "first", 5);
  check_string(elements[1], "second", 6);
  assert(elements[1].data == outcome.result.as.value.as.string.data);
  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);

  // Copying strings and arrays produces fresh mutable backing allocations.
  const char bytes[] = "a\0b";
  String source = {
    .data = bytes,
    .base = bytes,
    .length = 3,
    .nul_terminated = true,
  };
  String copied = {0};
  assert(copy_runtime_string(&interpreter, source, &copied));
  check_string(copied, "a\0b", 3);
  assert(copied.data != source.data);

  String sources[] = { source, {0} };
  Array source_array = {
    .element_type = TYPE_STRING,
    .length = 2,
    .data = sources,
    .base = sources,
  };
  Array copied_array = {0};
  assert(copy_runtime_array(&interpreter, source_array, &copied_array));
  check_array(copied_array, TYPE_STRING, 2);
  assert(copied_array.data != source_array.data);
  const String *strings = copied_array.data;
  check_string(strings[0], "a\0b", 3);
  assert(strings[0].data != source.data);
  assert(strings[1].data == NULL);
  assert(strings[1].base == NULL);
  assert(strings[1].length == 0);
  assert(!strings[1].immutable_ref);

  uint32_t numbers[] = { 1, 2, 3 };
  Array numeric_array = {
    .element_type = TYPE_U32,
    .length = 3,
    .data = numbers,
    .base = numbers,
  };
  Array copied_numeric_array = {0};
  assert(copy_runtime_array(&interpreter, numeric_array, &copied_numeric_array));
  check_array(copied_numeric_array, TYPE_U32, 3);
  assert(copied_numeric_array.data != numeric_array.data);
  assert(memcmp(copied_numeric_array.data, numbers, sizeof(numbers)) == 0);

  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);
  puts("Value reference metadata tests passed");
  return 0;
}
