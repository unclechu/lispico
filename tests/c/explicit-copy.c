#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"
#include "../../src/evaluate-copy.h"
#include "../../src/stdlib-immutable-refs.h"
#include "../../src/symbol.h"

static void run_program(Interpreter *interpreter, const char *program) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, program, strlen(program), &error));
  assert(evaluate(interpreter, NULL, NULL).kind == EVAL_OK);
}

static void clear_program(Interpreter *interpreter) {
  clean_evaluation(interpreter);
  clean_symbols(interpreter);
  clean_expressions(interpreter);
}

int main(void) {
  static Expression expressions[1024] = {0};
  static ExpressionAllocation expression_allocations[1024] = {0};
  static Symbol symbols[2048] = {0};
  static EvalRuntimeAllocation allocations[2048] = {0};
  static uint32_t deferred[128] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 1024,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 1024,
    .symbols = symbols,
    .symbol_capacity = 2048,
    .runtime_allocations = allocations,
    .runtime_allocation_capacity = 2048,
    .deferred_expression_indices = deferred,
    .deferred_expression_capacity = 128,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // The result is writable, independent storage even when the source is a view.
  run_program(
    &interpreter,
    "(var \"source\" \"hello\") "
    "(var \"view\" (immutable-ref source)) "
    "(var \"independent\" (copy view)) "
    "(assert (== (is-immutable-ref independent) false)) "
    "(str-set \"source\" 0 (str-char \"H\")) "
    "(assert (== view \"Hello\")) "
    "(assert (== independent \"hello\")) "
    "(str-set \"independent\" 0 (str-char \"w\")) "
    "(assert (== independent \"wello\")) "
    "(gc) "
    "(assert (== independent \"wello\"))"
  );
  const Symbol *view = symbol_lookup(&interpreter, "view");
  const Symbol *independent = symbol_lookup(&interpreter, "independent");
  assert(view != NULL && independent != NULL);
  assert(view->as.variable.as.string.data != independent->as.variable.as.string.data);
  assert(!independent->as.variable.as.string.immutable_ref);
  assert(independent->as.variable.as.string.base == independent->as.variable.as.string.data);
  assert(independent->as.variable.as.string.nul_terminated);
  clear_program(&interpreter);

  // Every string element receives its own writable buffer, regardless of
  // whether the original array or that element was an immutable reference.
  run_program(
    &interpreter,
    "(var \"source\" (arr (immutable-ref \"first\") \"second\")) "
    "(var \"borrowed\" (immutable-ref source)) "
    "(var \"independent\" (copy borrowed)) "
    "(assert (== (is-immutable-ref independent) false)) "
    "(assert (== (has-immutable-refs independent) false)) "
    "(assert (== (arr-get independent 0) \"first\")) "
    "(arr-set \"source\" 0 \"replaced\") "
    "(arr-set \"independent\" 1 \"changed\") "
    "(gc) "
    "(assert (== (arr-get independent 0) \"first\")) "
    "(assert (== (arr-get source 1) \"second\"))"
  );
  const Symbol *source = symbol_lookup(&interpreter, "source");
  independent = symbol_lookup(&interpreter, "independent");
  assert(source != NULL && independent != NULL);
  assert(source->as.array.data != independent->as.array.data);
  const String *original_strings = source->as.array.data;
  const String *copied_strings = independent->as.array.data;
  assert(!independent->as.array.immutable_ref);
  for (size_t i = 0; i < 2; ++i) {
    assert(!copied_strings[i].immutable_ref);
    assert(copied_strings[i].base == copied_strings[i].data);
    assert(copied_strings[i].nul_terminated);
    assert(copied_strings[i].data != original_strings[i].data);
  }
  clear_program(&interpreter);

  // A visible, non-NUL-terminated portion is copied by length and receives
  // its own terminator. Neither its original base nor prefix is retained.
  const char bytes[] = "prefixSUFFIXsuffix";
  String slice = {
    .data = bytes + 6,
    .base = bytes,
    .length = 6,
    .immutable_ref = true,
    .nul_terminated = false,
  };
  String copied_slice = {0};
  assert(copy_runtime_string_mutable(&interpreter, slice, &copied_slice));
  assert(!copied_slice.immutable_ref);
  assert(copied_slice.base == copied_slice.data);
  assert(copied_slice.nul_terminated);
  assert(copied_slice.length == 6);
  assert(memcmp(copied_slice.data, "SUFFIX", 6) == 0);
  assert(copied_slice.data[6] == '\0');
  assert(copied_slice.data != slice.data);
  clear_program(&interpreter);

  // Embedded NUL bytes are copied by length, not by C-string termination.
  const char binary[] = { 'a', '\0', 'b', '\0' };
  String binary_view = {
    .data = binary,
    .base = binary,
    .length = 3,
    .immutable_ref = true,
    .nul_terminated = true,
  };
  String binary_copy = {0};
  assert(copy_runtime_string_mutable(&interpreter, binary_view, &binary_copy));
  assert(!binary_copy.immutable_ref && binary_copy.nul_terminated);
  assert(binary_copy.data != binary_view.data && binary_copy.length == 3);
  assert(memcmp(binary_copy.data, binary, sizeof(binary)) == 0);
  clear_program(&interpreter);

  // Empty NULL-backed immutable strings and arrays become mutable copies.
  String empty_string = { .immutable_ref = true };
  String copied_empty = {0};
  assert(copy_runtime_string_mutable(&interpreter, empty_string, &copied_empty));
  assert(copied_empty.data == NULL && !copied_empty.immutable_ref);
  Array empty_array = { .element_type = TYPE_STRING, .immutable_ref = true };
  Array copied_empty_array = {0};
  assert(copy_runtime_array_mutable(&interpreter, empty_array, &copied_empty_array));
  assert(copied_empty_array.data == NULL && !copied_empty_array.immutable_ref);

  // Numeric arrays are copied independently, without creating immutable views.
  uint32_t data[] = { 1, 2, 3 };
  Array numeric = {
    .element_type = TYPE_U32,
    .length = 3,
    .data = data,
    .base = data,
    .immutable_ref = true,
  };
  Array copied_numeric = {0};
  assert(copy_runtime_array_mutable(&interpreter, numeric, &copied_numeric));
  assert(copied_numeric.data != numeric.data && !copied_numeric.immutable_ref);
  assert(memcmp(copied_numeric.data, numeric.data, sizeof(data)) == 0);
  clear_program(&interpreter);

  // Malformed nested storage must fail, not dereference an absent buffer.
  String invalid_element = { .length = 1, .immutable_ref = true };
  Array invalid_array = {
    .element_type = TYPE_STRING,
    .length = 1,
    .data = &invalid_element,
    .base = &invalid_element,
    .immutable_ref = true,
  };
  Array unused = {0};
  assert(!copy_runtime_array_mutable(&interpreter, invalid_array, &unused));
  clear_program(&interpreter);

  puts("Explicit copy tests passed");
  return 0;
}
