#include <assert.h>
#include <stdio.h>

#include "../../src/storage.h"
#include "../../src/stdlib-sequences.h"

static uint32_t get_depth(
  Datum (*fn)(Interpreter*, const Datum*, size_t),
  Interpreter *interpreter
) {
  Datum result = fn(interpreter, NULL, 0);
  assert(result.kind == SYM_VARIABLE);
  assert(result.as.value.type == TYPE_U32);
  return result.as.value.as.u32;
}

int main(void) {
  Interpreter interpreter = {0};

  // Ordinary `(do)` scopes do not belong to any function invocation.
  assert(get_depth(std_fn_depth, &interpreter) == 0);
  assert(get_depth(std_fn_return_depth, &interpreter) == 0);
  interpreter.current_sequence_depth = 3;
  assert(get_depth(std_fn_depth, &interpreter) == 0);
  assert(get_depth(std_fn_return_depth, &interpreter) == 0);

  // The implicit function scope has an absolute depth of four. Explicit
  // nested `(do)` scopes change the return count, not the function depth.
  EvalFunctionCallFrame outer = { .entry_sequence_depth = 3, .call_depth = 1 };
  interpreter.function_call = &outer;
  interpreter.current_sequence_depth = 4;
  assert(get_depth(std_fn_depth, &interpreter) == 4);
  assert(get_depth(std_fn_return_depth, &interpreter) == 1);
  interpreter.current_sequence_depth = 7;
  assert(get_depth(std_fn_depth, &interpreter) == 4);
  assert(get_depth(std_fn_return_depth, &interpreter) == 4);

  // Nested invocations have their own implicit `(do)` scope, including the
  // caller’s active depth when determining the absolute function depth.
  EvalFunctionCallFrame inner = {
    .entry_sequence_depth = 7,
    .call_depth = 2,
    .previous = &outer,
  };
  interpreter.function_call = &inner;
  interpreter.current_sequence_depth = 8;
  assert(get_depth(std_fn_depth, &interpreter) == 8);
  assert(get_depth(std_fn_return_depth, &interpreter) == 1);
  interpreter.current_sequence_depth = 10;
  assert(get_depth(std_fn_depth, &interpreter) == 8);
  assert(get_depth(std_fn_return_depth, &interpreter) == 3);

  // Restoring the previous invocation restores its absolute function depth.
  interpreter.function_call = inner.previous;
  interpreter.current_sequence_depth = 7;
  assert(get_depth(std_fn_depth, &interpreter) == 4);
  assert(get_depth(std_fn_return_depth, &interpreter) == 4);

  // Both functions reject arguments.
  Datum arg = { .kind = SYM_VARIABLE, .as.value = { .type = TYPE_U32, .as.u32 = 1 } };
  Datum failure = std_fn_depth(&interpreter, &arg, 1);
  assert(failure.kind == SYM_EMPTY);
  assert(interpreter.eval_outcome.kind == EVAL_FAIL);
  interpreter.eval_outcome.kind = EVAL_OK;
  failure = std_fn_return_depth(&interpreter, &arg, 1);
  assert(failure.kind == SYM_EMPTY);
  assert(interpreter.eval_outcome.kind == EVAL_FAIL);

  puts("Function depth tests passed");
  return 0;
}
