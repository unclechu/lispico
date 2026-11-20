#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/stdlib-arrays.h"

static Datum array_datum(ValueType type, uint32_t length, void *data) {
  return (Datum) {
    .kind = SYM_ARRAY,
    .as.array = { .element_type = type, .length = length, .data = data },
  };
}

int main(void) {
  static Expression expressions[16] = {0};
  static ExpressionAllocation expression_allocations[16] = {0};
  static Symbol symbols[256] = {0};
  static EvalRuntimeAllocation runtime_allocations[16] = {0};
  static uint32_t deferred_indices[16] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 16,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 16,
    .symbols = symbols,
    .symbol_capacity = 256,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 16,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 16,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  uint32_t first[] = { 1, 2 };
  uint32_t second[] = { 3 };
  Datum args[] = {
    array_datum(TYPE_U32, 2, first),
    array_datum(TYPE_U32, 1, second),
  };

  Datum result = std_arr_concat(&interpreter, args, 2);
  assert(interpreter.eval_outcome.kind == EVAL_OK);
  assert(result.kind == SYM_ARRAY);
  assert(result.as.array.element_type == TYPE_U32);
  assert(result.as.array.length == 3);
  assert(result.as.array.data != first);
  assert(((uint32_t *)result.as.array.data)[0] == 1);
  assert(((uint32_t *)result.as.array.data)[2] == 3);
  first[0] = 99;
  assert(((uint32_t *)result.as.array.data)[0] == 1);

  // A zero-length array keeps its element type and may have no data pointer.
  Datum empty = array_datum(TYPE_U32, 0, NULL);
  result = std_arr_concat(&interpreter, &empty, 1);
  assert(interpreter.eval_outcome.kind == EVAL_OK);
  assert(result.as.array.element_type == TYPE_U32);
  assert(result.as.array.length == 0);
  assert(result.as.array.data != NULL);

  // String data is copied independently, including embedded NUL bytes.
  char bytes[] = { 'a', '\0', 'b' };
  String strings[] = { { .data = bytes, .length = sizeof(bytes) } };
  Datum string_args[] = {
    array_datum(TYPE_STRING, 1, strings),
    array_datum(TYPE_STRING, 1, strings),
  };
  result = std_arr_concat(&interpreter, string_args, 2);
  assert(interpreter.eval_outcome.kind == EVAL_OK);
  assert(result.as.array.length == 2);
  const String *copies = result.as.array.data;
  assert(copies[0].data != strings[0].data);
  assert(copies[0].data != copies[1].data);
  assert(copies[0].length == 3);
  assert(copies[1].length == 3);
  bytes[0] = 'z';
  assert(memcmp(copies[0].data, "a\0b", 3) == 0);
  assert(memcmp(copies[1].data, "a\0b", 3) == 0);

  // Zero-initialized string entries stay unallocated in the result.
  String zero_string = { .data = NULL, .length = 0 };
  Datum zero_strings = array_datum(TYPE_STRING, 1, &zero_string);
  size_t previous_count = interpreter.runtime_allocation_count;
  result = std_arr_concat(&interpreter, &zero_strings, 1);
  assert(interpreter.eval_outcome.kind == EVAL_OK);
  assert(interpreter.runtime_allocation_count == previous_count + 1);
  assert(((const String *)result.as.array.data)[0].data == NULL);
  assert(((const String *)result.as.array.data)[0].length == 0);

  // Invalid arrays and an oversized result must fail before allocation.
  size_t allocated_count = interpreter.runtime_allocation_count;
  args[0] = array_datum(TYPE_U32, UINT32_MAX, first);
  result = std_arr_concat(&interpreter, args, 2);
  assert(interpreter.eval_outcome.kind == EVAL_FAIL);
  assert(result.kind == SYM_EMPTY);
  assert(interpreter.runtime_allocation_count == allocated_count);

  interpreter.eval_outcome.kind = EVAL_OK;
  args[0] = array_datum(TYPE_U32, 1, NULL);
  result = std_arr_concat(&interpreter, args, 2);
  assert(interpreter.eval_outcome.kind == EVAL_FAIL);
  assert(result.kind == SYM_EMPTY);
  assert(interpreter.runtime_allocation_count == allocated_count);

  interpreter.eval_outcome.kind = EVAL_OK;
  String bad_string = { .data = NULL, .length = 1 };
  Datum bad_array = array_datum(TYPE_STRING, 1, &bad_string);
  result = std_arr_concat(&interpreter, &bad_array, 1);
  assert(interpreter.eval_outcome.kind == EVAL_FAIL);
  assert(result.kind == SYM_EMPTY);
  assert(interpreter.runtime_allocation_count == allocated_count);

  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);

  puts("Array concatenation tests passed");
  return 0;
}
