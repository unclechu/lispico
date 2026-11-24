#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"
#include "../../src/evaluate-copy.h"
#include "../../src/symbol.h"

static void run_program(Interpreter *interpreter, const char *program, bool succeeds) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, program, strlen(program), &error));
  EvalOutcome outcome = evaluate(interpreter, NULL, NULL);
  assert((outcome.kind == EVAL_OK) == succeeds);
  if (!succeeds) assert(outcome.kind == EVAL_FAIL);
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

  // An immutable view shares backing storage but cannot change the permission
  // of the original writable variable. In-place edits remain visible.
  run_program(
    &interpreter,
    "(var \"text\" \"hello\") "
    "(var \"view\" (immutable-ref text)) "
    "(str-set \"text\" 0 72) "
    "(assert (== view \"Hello\")) "
    "(var \"numbers\" (arr 1 2 3)) "
    "(var \"number-view\" (immutable-ref numbers)) "
    "(arr-set \"numbers\" 1 9) "
    "(assert (== (arr-get number-view 1) 9))",
    true
  );
  const Symbol *text = symbol_lookup(&interpreter, "text");
  const Symbol *view = symbol_lookup(&interpreter, "view");
  const Symbol *numbers = symbol_lookup(&interpreter, "numbers");
  const Symbol *number_view = symbol_lookup(&interpreter, "number-view");
  assert(text != NULL && view != NULL && numbers != NULL && number_view != NULL);
  assert(!text->as.variable.as.string.immutable_ref);
  assert(view->as.variable.as.string.immutable_ref);
  assert(text->as.variable.as.string.data == view->as.variable.as.string.data);
  assert(text->as.variable.as.string.base == view->as.variable.as.string.base);
  assert(!numbers->as.array.immutable_ref);
  assert(number_view->as.array.immutable_ref);
  assert(numbers->as.array.data == number_view->as.array.data);
  assert(numbers->as.array.base == number_view->as.array.base);
  clear_program(&interpreter);

  // The array and its string elements have independent immutable flags.
  run_program(
    &interpreter,
    "(var \"words\" (arr (immutable-ref \"first\") \"second\")) "
    "(assert (== (is-immutable-ref words) false)) "
    "(assert (has-immutable-refs words)) "
    "(assert (is-immutable-ref (arr-get words 0))) "
    "(assert (== (is-immutable-ref (arr-get words 1)) false))",
    true
  );
  const Symbol *words = symbol_lookup(&interpreter, "words");
  assert(words != NULL && words->kind == SYM_ARRAY && !words->as.array.immutable_ref);
  const String *items = words->as.array.data;
  assert(items[0].immutable_ref && !items[1].immutable_ref);
  clear_program(&interpreter);

  // Function argument binding shares read-only views. An explicit GC retains
  // the original allocation after its writable owner is rebound.
  run_program(
    &interpreter,
    "(fn \"inspect\" (args \"v\") (do (assert (is-immutable-ref v)) (gc) v)) "
    "(var \"buffer\" (arr 4 5 6)) "
    "(var \"borrowed\" (inspect (immutable-ref buffer))) "
    "(override \"buffer\" (arr 8 9)) "
    "(gc) "
    "(assert (== (arr-get borrowed 0) 4))",
    true
  );
  clear_program(&interpreter);

  // A returned view of a function-local string keeps its runtime allocation
  // reachable even after that local scope has exited.
  run_program(
    &interpreter,
    "(fn \"make-view\" (do (local \"temporary\" \"created\") "
    "(immutable-ref temporary))) "
    "(var \"escaped\" (make-view)) "
    "(gc) "
    "(assert (== escaped \"created\"))",
    true
  );
  clear_program(&interpreter);

  // Mutable arguments and mutable array elements continue to be copied.
  run_program(
    &interpreter,
    "(fn \"mutate\" (args \"x\") (do (str-set \"x\" 0 88) x)) "
    "(var \"original\" \"abc\") "
    "(assert (== (mutate original) \"Xbc\")) "
    "(assert (== original \"abc\"))",
    true
  );
  clear_program(&interpreter);

  // The generic copy helper must not allocate when given an immutable view.
  String borrowed = {
    .data = "borrowed",
    .base = "borrowed",
    .length = 8,
    .immutable_ref = true,
    .nul_terminated = true,
  };
  String result = {0};
  assert(copy_runtime_string(&interpreter, borrowed, &result));
  assert(result.data == borrowed.data);
  assert(result.immutable_ref);
  assert(interpreter.runtime_allocation_count == 0);

  Array borrowed_array = {
    .element_type = TYPE_STRING,
    .length = 1,
    .data = &borrowed,
    .base = &borrowed,
    .immutable_ref = true,
  };
  Array copied_array = {0};
  assert(copy_runtime_array(&interpreter, borrowed_array, &copied_array));
  assert(copied_array.data == borrowed_array.data);
  assert(copied_array.immutable_ref);
  assert(interpreter.runtime_allocation_count == 0);

  // Even a NULL-backed empty string may carry an immutable-reference marker.
  // Copying a mutable container must preserve that element's permissions.
  String empty_view = { .immutable_ref = true };
  Array container = {
    .element_type = TYPE_STRING,
    .length = 1,
    .data = &empty_view,
    .base = &empty_view,
  };
  Array container_copy = {0};
  assert(copy_runtime_array(&interpreter, container, &container_copy));
  assert(container_copy.data != container.data);
  assert(((const String *)container_copy.data)[0].immutable_ref);
  clean_evaluation(&interpreter);

  puts("Immutable-reference tests passed");
  return 0;
}
