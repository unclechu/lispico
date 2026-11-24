#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/stdlib-arrays.h"
#include "../../src/stdlib-gc.h"
#include "../../src/stdlib-immutable-refs.h"
#include "../../src/stdlib-strings.h"
#include "../../src/stdlib-variables.h"
#include "../../src/evaluate-copy.h"

static Datum string_value(String value) {
  return (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_STRING, .as.string = value },
  };
}

static Datum text_value(const char *text) {
  return string_value((String) {
    .data = text,
    .base = text,
    .length = strlen(text),
    .nul_terminated = true,
  });
}

static Datum number(uint32_t value) {
  return (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = value },
  };
}

static Datum array_value(String *values, uint32_t length) {
  return (Datum) {
    .kind = SYM_ARRAY,
    .as.array = {
      .element_type = TYPE_STRING,
      .length = length,
      .data = values,
      .base = values,
    },
  };
}

static void expect_text(String value, const char *text) {
  assert(value.length == strlen(text));
  assert(value.data != NULL);
  assert(memcmp(value.data, text, value.length) == 0);
}

static void test_string_array_set(Interpreter *interpreter) {
  Symbol *symbol = symbol_define(interpreter, "destination", SYM_ARRAY);
  assert(symbol != NULL);
  String original[] = {
    { .data = "one", .base = "one", .length = 3, .nul_terminated = true },
    { .data = "two", .base = "two", .length = 3, .nul_terminated = true },
    { .data = "three", .base = "three", .length = 5, .nul_terminated = true },
  };
  symbol->as.array = array_value(original, 3).as.array;

  // A source overlapping the destination must be snapshotted before writes.
  // The original second element must not be overwritten before being read.
  const char *source_first = original[1].data;
  const char *source_second = original[2].data;
  Datum args[] = {
    text_value("destination"),
    array_value(original + 1, 2),
  };
  Datum result = std_set(interpreter, args, 2);
  assert(interpreter->eval_outcome.kind == EVAL_OK);
  assert(result.kind == SYM_ARRAY);
  expect_text(original[0], "two");
  expect_text(original[1], "three");
  assert(original[2].data == NULL && original[2].length == 0);
  assert(!original[0].immutable_ref && !original[1].immutable_ref);
  assert(original[0].data != source_first);
  assert(original[1].data != source_second);
  assert(interpreter->runtime_allocation_count == 2);

  // An immutable reference is stored without copying its character bytes.
  String borrowed[] = {
    { .data = "alpha", .base = "alpha", .length = 5,
      .immutable_ref = true, .nul_terminated = true },
  };
  args[1] = array_value(borrowed, 1);
  result = std_set(interpreter, args, 2);
  assert(interpreter->eval_outcome.kind == EVAL_OK);
  assert(result.kind == SYM_ARRAY);
  assert(original[0].data == borrowed[0].data);
  assert(original[0].immutable_ref);
  assert(original[1].data == NULL && original[2].data == NULL);
  assert(interpreter->runtime_allocation_count == 2);

  // Exercise the heap staging path (more than eight descriptors).
  String large_source[9] = {0};
  String large_target[9] = {0};
  for (uint32_t i = 0; i < 9; ++i) large_source[i] = borrowed[0];
  large_source[4] = (String) {0};
  symbol->as.array = array_value(large_target, 9).as.array;
  args[1] = array_value(large_source, 9);
  result = std_set(interpreter, args, 2);
  assert(interpreter->eval_outcome.kind == EVAL_OK);
  for (uint32_t i = 0; i < 9; ++i) {
    if (i == 4) {
      assert(large_target[i].data == NULL);
      assert(!large_target[i].immutable_ref);
    } else {
      assert(large_target[i].data == borrowed[0].data);
      assert(large_target[i].immutable_ref);
    }
  }
  assert(interpreter->runtime_allocation_count == 2);
  clean_symbols(interpreter);
  clean_evaluation(interpreter);
}

static void test_failed_array_set_is_atomic(void) {
  static Expression expressions[16] = {0};
  static ExpressionAllocation expression_allocations[16] = {0};
  static Symbol symbols[64] = {0};
  static EvalRuntimeAllocation allocations[1] = {0};
  static uint32_t deferred[16] = {0};
  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 16,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 16,
    .symbols = symbols,
    .symbol_capacity = 64,
    .runtime_allocations = allocations,
    .runtime_allocation_capacity = 1,
    .deferred_expression_indices = deferred,
    .deferred_expression_capacity = 16,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  Symbol *destination = symbol_define(&interpreter, "target", SYM_ARRAY);
  assert(destination != NULL);
  String old[] = {
    { .data = "unchanged", .base = "unchanged", .length = 9, .nul_terminated = true },
    { .data = "original", .base = "original", .length = 8, .nul_terminated = true },
  };
  destination->as.array = array_value(old, 2).as.array;
  String replacements[] = {
    { .data = "new", .base = "new", .length = 3, .nul_terminated = true },
    { .data = "another", .base = "another", .length = 7, .nul_terminated = true },
  };
  Datum args[] = { text_value("target"), array_value(replacements, 2) };

  // The first copy fits, but capacity exhaustion on the second must leave
  // the destination untouched. Failed allocations are still GC-tracked.
  Datum result = std_set(&interpreter, args, 2);
  assert(result.kind == SYM_EMPTY);
  assert(interpreter.eval_outcome.kind == EVAL_FAIL);
  expect_text(old[0], "unchanged");
  expect_text(old[1], "original");
  assert(interpreter.runtime_allocation_count == 1);

  interpreter.eval_outcome.kind = EVAL_OK;
  assert(std_gc(&interpreter, NULL, 0).as.value.as.u32 == 1);
  assert(interpreter.runtime_allocation_count == 0);

  // An invalid later element must also fail before any destination changes.
  replacements[1] = (String) { .length = 4 };
  result = std_set(&interpreter, args, 2);
  assert(result.kind == SYM_EMPTY);
  assert(interpreter.eval_outcome.kind == EVAL_FAIL);
  assert(interpreter.runtime_allocation_count == 0);
  expect_text(old[0], "unchanged");
  expect_text(old[1], "original");

  // The host-allocated staging path must also unwind cleanly on failure.
  // This makes the second runtime string copy fail after the first succeeds.
  String large_destination[9] = {0};
  String large_replacement[9] = {0};
  for (uint32_t i = 0; i < 9; ++i) {
    large_destination[i] = old[0];
    large_replacement[i] = (String) {
      .data = "borrowed",
      .base = "borrowed",
      .length = 8,
      .immutable_ref = true,
      .nul_terminated = true,
    };
  }
  large_replacement[0] = replacements[0];
  large_replacement[1] = (String) {
    .data = "second",
    .base = "second",
    .length = 6,
    .nul_terminated = true,
  };
  destination->as.array = array_value(large_destination, 9).as.array;
  args[1] = array_value(large_replacement, 9);
  interpreter.eval_outcome.kind = EVAL_OK;
  result = std_set(&interpreter, args, 2);
  assert(result.kind == SYM_EMPTY);
  assert(interpreter.eval_outcome.kind == EVAL_FAIL);
  assert(interpreter.runtime_allocation_count == 1);
  for (uint32_t i = 0; i < 9; ++i) {
    assert(large_destination[i].data == old[0].data);
    assert(!large_destination[i].immutable_ref);
  }

  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
}

static void test_allocation_counts(Interpreter *interpreter) {
  // Explicit construction allocates, while immutable references and nested
  // slices only copy descriptors. Numbers below are deterministic allocation
  // deltas, not timing claims.
  char *data = allocate_runtime_data(interpreter, 6);
  assert(data != NULL);
  memcpy(data, "hello", 6);
  String writable = { .data = data, .base = data, .length = 5, .nul_terminated = true };
  Datum arg = string_value(writable);
  uint32_t before = interpreter->runtime_allocation_count;

  Datum borrowed = std_immutable_ref(interpreter, &arg, 1);
  assert(borrowed.as.value.as.string.data == data);
  assert(borrowed.as.value.as.string.immutable_ref);
  assert(interpreter->runtime_allocation_count == before);

  Datum args[] = { borrowed, number(1), number(3) };
  Datum slice = std_str_slice(interpreter, args, 3);
  assert(slice.as.value.as.string.data == data + 1);
  assert(slice.as.value.as.string.base == data);
  assert(interpreter->runtime_allocation_count == before);

  Datum copy = std_copy(interpreter, &slice, 1);
  assert(copy.kind == SYM_VARIABLE);
  assert(!copy.as.value.as.string.immutable_ref);
  assert(copy.as.value.as.string.data != data + 1);
  assert(interpreter->runtime_allocation_count == before + 1);

  String borrowed_elements[] = {
    { .data = data + 1, .base = data, .length = 3, .immutable_ref = true },
    { .data = data + 2, .base = data, .length = 3, .immutable_ref = true },
  };
  Datum pair = array_value(borrowed_elements, 2);
  before = interpreter->runtime_allocation_count;
  Datum concat_args[] = { pair, pair };
  Datum concat = std_arr_concat(interpreter, concat_args, 2);
  assert(concat.kind == SYM_ARRAY);
  assert(concat.as.array.length == 4);
  assert(interpreter->runtime_allocation_count == before + 1);
  assert(((String *)concat.as.array.data)[0].data == data + 1);

  before = interpreter->runtime_allocation_count;
  Datum deep = std_copy(interpreter, &pair, 1);
  assert(deep.kind == SYM_ARRAY);
  assert(!deep.as.array.immutable_ref);
  assert(interpreter->runtime_allocation_count == before + 3);
  const String *items = deep.as.array.data;
  assert(!items[0].immutable_ref && !items[1].immutable_ref);
  assert(items[0].data != borrowed_elements[0].data);

  // Enumeration has one descriptor allocation, independent of symbol count.
  Symbol *one = symbol_define(interpreter, "one", SYM_VARIABLE);
  Symbol *two = symbol_define(interpreter, "two", SYM_VARIABLE);
  assert(one != NULL && two != NULL);
  before = interpreter->runtime_allocation_count;
  Datum names = std_variables(interpreter, NULL, 0);
  assert(names.kind == SYM_ARRAY && names.as.array.length == 2);
  assert(interpreter->runtime_allocation_count == before + 1);
  const String *name_strings = names.as.array.data;
  assert(name_strings[0].immutable_ref && name_strings[1].immutable_ref);
  assert(name_strings[0].base == name_strings[0].data);

  before = interpreter->runtime_allocation_count;
  Datum reference_arg = text_value("one");
  Datum reference = std_var_ref(interpreter, &reference_arg, 1);
  assert(reference.kind == SYM_VARIABLE);
  assert(reference.as.value.as.string.data == one->name);
  assert(interpreter->runtime_allocation_count == before);

  printf(
    "Allocation audit: immutable-ref 0, str-slice 0, copy(string) 1, "
    "arr-concat(immutable strings) 1, copy(string array of 2) 3, "
    "variables(2) 1, var-ref 0\n"
  );
  clean_evaluation(interpreter);
  clean_symbols(interpreter);
}

int main(void) {
  static Expression expressions[64] = {0};
  static ExpressionAllocation expression_allocations[64] = {0};
  static Symbol symbols[256] = {0};
  static EvalRuntimeAllocation allocations[256] = {0};
  static uint32_t deferred[32] = {0};
  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 64,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 64,
    .symbols = symbols,
    .symbol_capacity = 256,
    .runtime_allocations = allocations,
    .runtime_allocation_capacity = 256,
    .deferred_expression_indices = deferred,
    .deferred_expression_capacity = 32,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));
  test_string_array_set(&interpreter);
  test_failed_array_set_is_atomic();
  test_allocation_counts(&interpreter);
  puts("Immutable-reference audit tests passed");
  return 0;
}
