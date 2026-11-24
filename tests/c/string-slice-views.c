#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/evaluate.h"
#include "../../src/evaluate-copy.h"
#include "../../src/stdlib-gc.h"
#include "../../src/stdlib-strings.h"

static Datum string_datum(String string) {
  return (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_STRING, .as.string = string },
  };
}

static Datum u32_datum(uint32_t value) {
  return (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = value },
  };
}

static String expect_view(Datum datum, const char *contents, size_t length) {
  assert(datum.kind == SYM_VARIABLE);
  assert(datum.as.value.type == TYPE_STRING);
  String string = datum.as.value.as.string;
  assert(string.immutable_ref);
  assert(string.length == length);
  if (length != 0) {
    assert(string.data != NULL);
    assert(memcmp(string.data, contents, length) == 0);
  }
  return string;
}

int main(void) {
  static Expression expressions[128] = {0};
  static ExpressionAllocation expression_allocations[128] = {0};
  static Symbol symbols[256] = {0};
  static EvalRuntimeAllocation allocations[256] = {0};
  static uint32_t deferred[16] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 128,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 128,
    .symbols = symbols,
    .symbol_capacity = 256,
    .runtime_allocations = allocations,
    .runtime_allocation_capacity = 256,
    .deferred_expression_indices = deferred,
    .deferred_expression_capacity = 16,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // Construct one mutable backing buffer. None of the slicing operations
  // should add a runtime allocation or make a mutable alias.
  char *buffer = allocate_runtime_data(&interpreter, 7);
  assert(buffer != NULL);
  memcpy(buffer, "Hello!", 7);
  String original = {
    .data = buffer,
    .base = buffer,
    .length = 6,
    .nul_terminated = true,
  };
  Datum args[] = { string_datum(original), u32_datum(1), u32_datum(4) };
  uint32_t allocation_count = interpreter.runtime_allocation_count;
  String middle = expect_view(std_str_slice(&interpreter, args, 3), "ello", 4);
  assert(middle.data == buffer + 1);
  assert(middle.base == buffer);
  assert(!middle.nul_terminated);
  assert(interpreter.runtime_allocation_count == allocation_count);

  // Nested views retain the original base, not the intermediate view pointer.
  args[0] = string_datum(middle);
  args[1] = u32_datum(1);
  args[2] = u32_datum(2);
  String nested = expect_view(std_str_slice(&interpreter, args, 3), "ll", 2);
  assert(nested.data == buffer + 2);
  assert(nested.base == buffer);
  assert(!nested.nul_terminated);

  args[0] = string_datum(original);
  args[1] = u32_datum(2);
  String suffix = expect_view(std_str_drop(&interpreter, args, 2), "llo!", 4);
  assert(suffix.data == buffer + 2);
  assert(suffix.base == buffer);
  assert(suffix.nul_terminated);

  String prefix = expect_view(std_str_take(&interpreter, args, 2), "He", 2);
  assert(prefix.data == buffer);
  assert(prefix.base == buffer);
  assert(!prefix.nul_terminated);

  // Zero-length ranges can point inside the buffer or at its trailing NUL.
  args[1] = u32_datum(0);
  String empty_prefix = expect_view(std_str_take(&interpreter, args, 2), "", 0);
  assert(empty_prefix.data == buffer);
  assert(!empty_prefix.nul_terminated);
  args[1] = u32_datum(6);
  String empty_suffix = expect_view(std_str_drop(&interpreter, args, 2), "", 0);
  assert(empty_suffix.data == buffer + 6);
  assert(empty_suffix.base == buffer);
  assert(empty_suffix.nul_terminated);

  // Unallocated empty string-array elements still produce read-only empty
  // views, without arithmetic on NULL or any allocation.
  args[0] = string_datum((String) {0});
  args[1] = u32_datum(0);
  String empty = expect_view(std_str_take(&interpreter, args, 2), "", 0);
  assert(empty.data == NULL);
  assert(empty.base == NULL);
  assert(!empty.nul_terminated);
  assert(interpreter.runtime_allocation_count == allocation_count);

  // In-place changes through the writable source are visible in every view.
  buffer[2] = 'L';
  assert(memcmp(nested.data, "Ll", 2) == 0);

  // Copies of interior, unterminated views have independent mutable storage.
  String copied = {0};
  assert(copy_runtime_string_mutable(&interpreter, middle, &copied));
  assert(!copied.immutable_ref);
  assert(copied.nul_terminated);
  assert(copied.base == copied.data && copied.data != middle.data);
  assert(memcmp(copied.data, "eLlo", 4) == 0);
  assert(copied.data[4] == '\0');

  // GC preserves the original allocation via a slice’s base, and collects
  // it once no root retains that base. The independent copy remains rooted.
  Datum copied_root = string_datum(copied);
  EvalGCRootFrame root = { .values = &copied_root, .count = 1 };
  interpreter.gc_roots = &root;
  interpreter.eval_outcome.result = string_datum(nested);
  Datum gc_result = std_gc(&interpreter, NULL, 0);
  assert(gc_result.kind == SYM_VARIABLE);
  assert(interpreter.runtime_allocation_count == 2);
  assert(memcmp(nested.data, "Ll", 2) == 0);
  interpreter.gc_roots = NULL;
  interpreter.eval_outcome.result = copied_root;
  gc_result = std_gc(&interpreter, NULL, 0);
  assert(interpreter.runtime_allocation_count == 1);
  assert(memcmp(copied.data, "eLlo", 4) == 0);
  interpreter.eval_outcome.result = (Datum) { .kind = SYM_EMPTY };
  gc_result = std_gc(&interpreter, NULL, 0);
  assert(interpreter.runtime_allocation_count == 0);

  clean_evaluation(&interpreter);
  puts("String slice view tests passed");
  return 0;
}
