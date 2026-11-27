#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"

static void check_program(
  Interpreter *interpreter,
  const char *source,
  EvalOutcomeKind expected,
  const char *failure_text
) {
  char parse_error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, source, strlen(source), &parse_error));
  EvalOutcome outcome = evaluate(interpreter, NULL, NULL);
  if (outcome.kind != expected) {
    fprintf(stderr, "Unexpected result for %s: %s\n", source, outcome.as.fail_msg);
    assert(outcome.kind == expected);
  }
  if (failure_text != NULL) {
    assert(outcome.kind == EVAL_FAIL);
    assert(strstr(outcome.as.fail_msg, failure_text) != NULL);
    assert(outcome.failure_expression_index != EXPR_NONE);
  }

  // Every loop, function, sequence, and GC root must be popped on all paths.
  assert(interpreter->loop_frame == NULL);
  assert(interpreter->function_call == NULL);
  assert(interpreter->local_scope == NULL);
  assert(interpreter->active_local_scope == NULL);
  assert(interpreter->gc_roots == NULL);
  assert(interpreter->current_sequence_depth == 0);
  assert(interpreter->pending_leave_count == 0);
  assert(interpreter->deferred_expression_count == 0);

  clean_evaluation(interpreter);
  clean_symbols(interpreter);
  clean_expressions(interpreter);
}

int main(void) {
  static Expression expressions[4096] = {0};
  static ExpressionAllocation expression_allocations[1024] = {0};
  static Symbol symbols[1024] = {0};
  static EvalRuntimeAllocation allocations[512] = {0};
  static uint32_t deferred_indices[256] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 4096,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 1024,
    .symbols = symbols,
    .symbol_capacity = 1024,
    .runtime_allocations = allocations,
    .runtime_allocation_capacity = 512,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 256,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // Finite, indefinite, and conditional loops, including zero iterations.
  check_program(
    &interpreter,
    "(var \"i\" 0) (repeat 0 (fail \"unreached\")) "
    "(repeat 12 (set \"i\" (+ i 1))) "
    "(while (< i 15) (set \"i\" (+ i 1))) "
    "(assert (== i 15))",
    EVAL_OK, NULL
  );
  check_program(
    &interpreter,
    "(var \"i\" 0) (repeat "
    "  (do (set \"i\" (+ i 1)) (if (== i 4) (break 1)))) "
    "(assert (== i 4))",
    EVAL_OK, NULL
  );
  check_program(
    &interpreter,
    "(var \"sum\" 0) "
    "(repeat 4 (repeat 6 (set \"sum\" (+ sum 1)))) "
    "(assert (== sum 24))",
    EVAL_OK, NULL
  );

  // Each iteration is a new implicit scope. A defer uses that iteration’s
  // local values, runs before scope cleanup, and does not accumulate.
  check_program(
    &interpreter,
    "(var \"sum\" 0) (repeat 5 "
    "  (do (local \"x\" (+ sum 1)) "
    "      (defer (set \"sum\" (+ sum x))))) "
    "(assert (== sum 31))",
    EVAL_OK, NULL
  );
  check_program(
    &interpreter,
    "(var \"sum\" 0) "
    "(repeat 5 (defer (set \"sum\" (+ sum 1)))) "
    "(assert (== sum 5))",
    EVAL_OK, NULL
  );

  // Explicit depths, nested breaks, and direct leave use the same unwind.
  check_program(
    &interpreter,
    "(var \"n\" 0) (repeat 20 (repeat 20 "
    " (do (assert (== (loop-break-depth 1) 2)) "
    "     (assert (== (loop-break-depth 2) 3)) "
    "     (set \"n\" (+ n 1)) (break 2)))) "
    "(assert (== n 1))",
    EVAL_OK, NULL
  );
  check_program(
    &interpreter,
    "(var \"n\" 0) (repeat (repeat "
    "  (do (set \"n\" (+ n 1)) (leave (loop-break-depth 2))))) "
    "(assert (== n 1))",
    EVAL_OK, NULL
  );
  check_program(
    &interpreter,
    "(var \"n\" 0) (while "
    "  (do (set \"n\" (+ n 1)) (if (== n 2) (break)) true) "
    "  true) (assert (== n 2))",
    EVAL_OK, NULL
  );

  // A function can return from an arbitrary number of nested loops.
  check_program(
    &interpreter,
    "(fn \"f\" (repeat 10 (repeat 10 (do (return 42))))) "
    "(assert (== (f) 42))",
    EVAL_OK, NULL
  );
  check_program(
    &interpreter,
    "(fn \"f\" (while (return 17) (fail \"unreached\"))) "
    "(assert (== (f) 17))",
    EVAL_OK, NULL
  );
  check_program(
    &interpreter,
    "(fn \"f\" (repeat (do (leave (fn-return-depth) 18)))) "
    "(assert (== (f) 18))",
    EVAL_OK, NULL
  );
  check_program(
    &interpreter,
    "(fn \"f\" (repeat 2 (return))) "
    "(assert (== (type-of (f)) void))",
    EVAL_OK, NULL
  );

  // A loop inside a callee does not interfere with the caller’s loop.
  check_program(
    &interpreter,
    "(fn \"inner\" (repeat (break))) "
    "(var \"n\" 0) (repeat 3 "
    "  (do (inner) (set \"n\" (+ n 1)))) "
    "(assert (== n 3))",
    EVAL_OK, NULL
  );

  // The evaluator must fully unwind loop frames after errors, so a later
  // independent evaluation can safely use the same interpreter storage.
  check_program(&interpreter, "(while)", EVAL_FAIL, "expects exactly 2");
  check_program(&interpreter, "(while true)", EVAL_FAIL, "expects exactly 2");
  check_program(&interpreter, "(while true 1 2)", EVAL_FAIL, "expects exactly 2");
  check_program(&interpreter, "(repeat)", EVAL_FAIL, "expects 1 or 2");
  check_program(&interpreter, "(repeat 1 2 3)", EVAL_FAIL, "expects 1 or 2");
  check_program(&interpreter, "(repeat -1 123)", EVAL_FAIL, "count must be u32");
  check_program(&interpreter, "(repeat true 123)", EVAL_OK, NULL);
  check_program(&interpreter, "(while \"bad\" true)", EVAL_FAIL, "condition must be a numeric");
  check_program(&interpreter, "(while empty true)", EVAL_FAIL, "condition must be a numeric");
  check_program(&interpreter, "(break)", EVAL_FAIL, "exceeds enclosing loops");
  check_program(&interpreter, "(loop-break-depth)", EVAL_FAIL, "exceeds enclosing loops");
  check_program(&interpreter, "(repeat 1 (break 0))", EVAL_FAIL, "greater than 0");
  check_program(&interpreter, "(repeat 1 (break 2))", EVAL_FAIL, "exceeds enclosing loops");
  check_program(&interpreter, "(repeat 1 (break -1))", EVAL_FAIL, "must be u32");
  check_program(&interpreter, "(repeat 1 (break 1 2))", EVAL_FAIL, "expects 0 or 1");
  check_program(&interpreter, "(repeat 1 (loop-break-depth 0))", EVAL_FAIL, "greater than 0");
  check_program(&interpreter, "(repeat 1 (loop-break-depth 2))", EVAL_FAIL, "exceeds enclosing loops");
  check_program(&interpreter, "(repeat 1 (loop-break-depth 1 2))", EVAL_FAIL, "expects 0 or 1");
  check_program(
    &interpreter,
    "(fn \"f\" (break)) (repeat 1 (f))",
    EVAL_FAIL, "exceeds enclosing loops"
  );
  check_program(
    &interpreter,
    "(fn \"f\" (loop-break-depth)) (repeat 1 (f))",
    EVAL_FAIL, "exceeds enclosing loops"
  );
  check_program(
    &interpreter,
    "(repeat 1 (defer (break)))",
    EVAL_FAIL, "deferred expression cannot interrupt"
  );
  check_program(
    &interpreter,
    "(repeat 1 (defer (return 3)))",
    EVAL_FAIL, "requires an active (fn) call"
  );
  check_program(&interpreter, "(repeat 1 (fail \"stop\"))", EVAL_FAIL, "stop");
  check_program(&interpreter, "(repeat 0 (fail \"stop\"))", EVAL_OK, NULL);

  puts("Loop tests passed");
  return 0;
}
