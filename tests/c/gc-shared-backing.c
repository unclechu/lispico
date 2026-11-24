#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/stdlib-gc.h"
#include "../../src/evaluate.h"
#include "../../src/symbol.h"

// Verify retention through backing pointers rather than visible view pointers.
// The public immutable-reference operations are not implemented yet; these
// descriptors model the views that the next implementation steps will produce.
static void expect_gc(Interpreter *interpreter, uint32_t freed, uint32_t remaining) {
  Datum result = std_gc(interpreter, NULL, 0);
  assert(result.kind == SYM_VARIABLE);
  assert(result.as.value.type == TYPE_U32);
  assert(result.as.value.as.u32 == freed);
  assert(interpreter->runtime_allocation_count == remaining);
}

int main(void) {
  static Expression expressions[8] = {0};
  static ExpressionAllocation expression_allocations[8] = {0};
  static Symbol symbols[64] = {0};
  static EvalRuntimeAllocation allocations[64] = {0};
  static uint32_t deferred_indices[8] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 8,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 8,
    .symbols = symbols,
    .symbol_capacity = 64,
    .runtime_allocations = allocations,
    .runtime_allocation_capacity = 64,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 8,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // A result containing only an interior string pointer retains its backing
  // allocation across repeated GC calls. Unrelated storage is still freed.
  char *text = allocate_runtime_data(&interpreter, 16);
  char *unused = allocate_runtime_data(&interpreter, 4);
  assert(text != NULL && unused != NULL);
  memcpy(text, "Hello, World!", 14);
  interpreter.eval_outcome.result = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_STRING,
      .as.string = {
        .data = text + 7,
        .base = text,
        .length = 5,
        .immutable_ref = true,
        .nul_terminated = false,
      },
    },
  };
  expect_gc(&interpreter, 1, 1);
  assert(memcmp(interpreter.eval_outcome.result.as.value.as.string.data, "World", 5) == 0);
  expect_gc(&interpreter, 0, 1);
  interpreter.eval_outcome.result = (Datum) { .kind = SYM_EMPTY };
  expect_gc(&interpreter, 1, 0);

  // Even an empty interior slice can be the sole reference to backing storage.
  text = allocate_runtime_data(&interpreter, 5);
  assert(text != NULL);
  memcpy(text, "abcd", 5);
  interpreter.eval_outcome.result = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_STRING,
      .as.string = {
        .data = text + 2,
        .base = text,
        .length = 0,
        .immutable_ref = true,
      },
    },
  };
  expect_gc(&interpreter, 0, 1);
  interpreter.eval_outcome.result = (Datum) { .kind = SYM_EMPTY };
  expect_gc(&interpreter, 1, 0);

  // Symbols containing interior string views retain the original allocation.
  Symbol *global = symbol_define(&interpreter, "global-view", SYM_VARIABLE);
  assert(global != NULL);
  text = allocate_runtime_data(&interpreter, 10);
  assert(text != NULL);
  memcpy(text, "012345678", 10);
  global->as.variable = (Value) {
    .type = TYPE_STRING,
    .as.string = {
      .data = text + 3,
      .base = text,
      .length = 4,
      .immutable_ref = true,
    },
  };
  expect_gc(&interpreter, 0, 1);
  assert(memcmp(global->as.variable.as.string.data, "3456", 4) == 0);
  global->as.variable = (Value) { .type = TYPE_VOID };
  expect_gc(&interpreter, 1, 0);

  // An array view must retain its backing allocation even if it begins at an
  // interior element. Numeric arrays contain no additional storage pointers.
  uint32_t *numbers = allocate_runtime_data(&interpreter, 5 * sizeof(*numbers));
  assert(numbers != NULL);
  for (uint32_t i = 0; i < 5; ++i) numbers[i] = i + 10;
  interpreter.eval_outcome.result = (Datum) {
    .kind = SYM_ARRAY,
    .as.array = {
      .element_type = TYPE_U32,
      .length = 2,
      .data = numbers + 2,
      .base = numbers,
      .immutable_ref = true,
    },
  };
  expect_gc(&interpreter, 0, 1);
  assert(((const uint32_t *)interpreter.eval_outcome.result.as.array.data)[0] == 12);
  interpreter.eval_outcome.result = (Datum) { .kind = SYM_EMPTY };
  expect_gc(&interpreter, 1, 0);

  // A string-array view must retain both the descriptor allocation and each
  // visible string element’s backing allocation, including interior slices.
  String *descriptors = allocate_runtime_data(&interpreter, 4 * sizeof(*descriptors));
  text = allocate_runtime_data(&interpreter, 12);
  char *other_text = allocate_runtime_data(&interpreter, 8);
  char *hidden_text = allocate_runtime_data(&interpreter, 7);
  assert(descriptors != NULL && text != NULL && other_text != NULL && hidden_text != NULL);
  memset(descriptors, 0, 4 * sizeof(*descriptors));
  memcpy(text, "hello world", 12);
  memcpy(other_text, "another", 8);
  descriptors[0] = (String) {
    .data = hidden_text,
    .base = hidden_text,
    .length = 6,
  };
  descriptors[1] = (String) {
    .data = text + 6,
    .base = text,
    .length = 5,
    .immutable_ref = true,
    .nul_terminated = true,
  };
  descriptors[2] = (String) {
    .data = other_text + 1,
    .base = other_text,
    .length = 3,
    .immutable_ref = true,
  };
  interpreter.eval_outcome.result = (Datum) {
    .kind = SYM_ARRAY,
    .as.array = {
      .element_type = TYPE_STRING,
      .length = 2,
      .data = descriptors + 1,
      .base = descriptors,
      .immutable_ref = true,
    },
  };
  expect_gc(&interpreter, 1, 3);
  assert(memcmp(((const String *)interpreter.eval_outcome.result.as.array.data)[0].data, "world", 5) == 0);
  assert(memcmp(((const String *)interpreter.eval_outcome.result.as.array.data)[1].data, "not", 3) == 0);
  interpreter.eval_outcome.result = (Datum) { .kind = SYM_EMPTY };
  expect_gc(&interpreter, 3, 0);

  // A mutable array of descriptors may contain immutable string views. The
  // outer array’s immutability flag does not affect GC traversal.
  descriptors = allocate_runtime_data(&interpreter, sizeof(*descriptors));
  text = allocate_runtime_data(&interpreter, 6);
  assert(descriptors != NULL && text != NULL);
  memcpy(text, "hello", 6);
  descriptors[0] = (String) {
    .data = text + 1,
    .base = text,
    .length = 3,
    .immutable_ref = true,
  };
  global->kind = SYM_ARRAY;
  global->as.array = (Array) {
    .element_type = TYPE_STRING,
    .length = 1,
    .data = descriptors,
    .base = descriptors,
    .immutable_ref = false,
  };
  expect_gc(&interpreter, 0, 2);
  global->kind = SYM_VARIABLE;
  global->as.variable = (Value) { .type = TYPE_VOID };
  expect_gc(&interpreter, 2, 0);

  // Temporary evaluator roots protect views even if no symbol or final result
  // mentions their storage. Multiple frames are traversed through `previous`.
  text = allocate_runtime_data(&interpreter, 6);
  numbers = allocate_runtime_data(&interpreter, 3 * sizeof(*numbers));
  assert(text != NULL && numbers != NULL);
  memcpy(text, "abcde", 6);
  Datum temporary_values[] = {
    {
      .kind = SYM_VARIABLE,
      .as.value = {
        .type = TYPE_STRING,
        .as.string = { .data = text + 1, .base = text, .length = 3, .immutable_ref = true },
      },
    },
    {
      .kind = SYM_ARRAY,
      .as.array = {
        .element_type = TYPE_U32,
        .length = 1,
        .data = numbers + 1,
        .base = numbers,
        .immutable_ref = true,
      },
    },
  };
  EvalGCRootFrame first_root = { .values = temporary_values, .count = 1 };
  EvalGCRootFrame second_root = {
    .values = temporary_values + 1,
    .count = 1,
    .previous = &first_root,
  };
  interpreter.gc_roots = &second_root;
  expect_gc(&interpreter, 0, 2);
  interpreter.gc_roots = &first_root;
  expect_gc(&interpreter, 1, 1);
  interpreter.gc_roots = NULL;
  expect_gc(&interpreter, 1, 0);

  // Saved local symbol states from recursive `(do)` activations are also
  // roots, even when the symbol’s currently installed state has been replaced.
  text = allocate_runtime_data(&interpreter, 8);
  assert(text != NULL);
  memcpy(text, "abcdefg", 8);
  EvalSavedLocalSymbol saved_local = {
    .saved = {
      .kind = SYM_VARIABLE,
      .as.variable = {
        .type = TYPE_STRING,
        .as.string = { .data = text + 2, .base = text, .length = 3, .immutable_ref = true },
      },
    },
  };
  EvalLocalScopeFrame hidden_caller = {
    .saved_locals = &saved_local,
    .saved_local_count = 1,
  };
  EvalLocalScopeFrame current_scope = {
    .previous_active = &hidden_caller,
  };
  interpreter.active_local_scope = &current_scope;
  expect_gc(&interpreter, 0, 1);
  assert(memcmp(saved_local.saved.as.variable.as.string.data, "cde", 3) == 0);
  interpreter.active_local_scope = NULL;
  expect_gc(&interpreter, 1, 0);

  // Expression-backed, literal, and host-owned buffers are never collected.
  // They are not registered as evaluator runtime allocations.
  static const char literal[] = "static";
  interpreter.eval_outcome.result = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_STRING,
      .as.string = { .data = literal + 1, .base = literal, .length = 3, .immutable_ref = true },
    },
  };
  text = allocate_runtime_data(&interpreter, 3);
  assert(text != NULL);
  expect_gc(&interpreter, 1, 0);
  assert(memcmp(interpreter.eval_outcome.result.as.value.as.string.data, "tat", 3) == 0);

  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  puts("GC backing-storage tests passed");
  return 0;
}
