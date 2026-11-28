#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"

static void run_program(
  Interpreter *interpreter,
  const char *source,
  EvalOutcomeKind expected_kind,
  const char *failure_fragment
) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, source, strlen(source), &error));

  EvalOutcome outcome = evaluate(interpreter, NULL, NULL);
  assert(outcome.kind == expected_kind);
  if (failure_fragment != NULL) {
    assert(strstr(outcome.as.fail_msg, failure_fragment) != NULL);
  }

  assert(interpreter->local_scope == NULL);
  assert(interpreter->active_local_scope == NULL);
  assert(interpreter->function_call == NULL);
  assert(interpreter->gc_roots == NULL);
  assert(interpreter->current_sequence_depth == 0);
}

static void clear_program(Interpreter *interpreter) {
  clean_evaluation(interpreter);
  clean_symbols(interpreter);
  clean_expressions(interpreter);
}

int main(void) {
  static Expression expressions[2048] = {0};
  static ExpressionAllocation expression_allocations[1024] = {0};
  static Symbol symbols[2048] = {0};
  static EvalRuntimeAllocation allocations[2048] = {0};
  static uint32_t deferred[128] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 2048,
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

  // Plain literals do not need explicit `(immutable-ref)` calls. Named and
  // unnamed arguments all borrow storage; numeric values stay numeric.
  run_program(
    &interpreter,
    "(fn \"inspect\" (immutable-var-args \"first\") (do "
    "  (assert (== argc 4)) "
    "  (assert (is-immutable-ref first)) "
    "  (assert (is-immutable-ref (get-arg 0))) "
    "  (assert (is-immutable-ref (get-arg 1))) "
    "  (assert (is-immutable-ref (get-arg 2))) "
    "  (assert (== (get-arg 3) 17)) "
    "  (assert (== (type-of (get-arg 3)) u32)) "
    "  (return))) "
    "(assert (== (gc) 0)) "
    "(inspect \"a\" \"b\" \"c\" 17) "
    "(assert (== (gc) 0))",
    EVAL_OK,
    NULL
  );
  clear_program(&interpreter);

  // Immutable named locals and get-arg values share original backing storage;
  // the writable caller’s descriptors are not modified by the call.
  run_program(
    &interpreter,
    "(var \"source\" \"hello\") "
    "(var \"items\" (arr 1 2 3)) "
    "(fn \"view-text\" (immutable-args \"text\") text) "
    "(fn \"view-array\" (immutable-args \"arr\") arr) "
    "(var \"text-view\" (view-text source)) "
    "(var \"array-view\" (view-array items)) "
    "(assert (is-immutable-ref text-view)) "
    "(assert (is-immutable-ref array-view)) "
    "(assert (== (is-immutable-ref source) false)) "
    "(assert (== (is-immutable-ref items) false)) "
    "(str-set \"source\" 0 72) "
    "(arr-set \"items\" 0 9) "
    "(assert (== text-view \"Hello\")) "
    "(assert (== (arr-get array-view 0) 9))",
    EVAL_OK,
    NULL
  );
  const Symbol *source = symbol_lookup(&interpreter, "source");
  const Symbol *text_view = symbol_lookup(&interpreter, "text-view");
  const Symbol *items = symbol_lookup(&interpreter, "items");
  const Symbol *array_view = symbol_lookup(&interpreter, "array-view");
  assert(source != NULL && text_view != NULL && items != NULL && array_view != NULL);
  assert(source->as.variable.as.string.data == text_view->as.variable.as.string.data);
  assert(source->as.variable.as.string.base == text_view->as.variable.as.string.base);
  assert(items->as.array.data == array_view->as.array.data);
  assert(items->as.array.base == array_view->as.array.base);
  clear_program(&interpreter);

  // Both ordinary declarations retain their old mutable-copy behavior.
  run_program(
    &interpreter,
    "(fn \"ordinary\" (args \"value\") (do "
    "  (assert (== (is-immutable-ref value) false)) "
    "  (assert (== (is-immutable-ref (get-arg 0)) false)))) "
    "(fn \"ordinary-variadic\" (var-args \"first\") (do "
    "  (assert (== (is-immutable-ref first) false)) "
    "  (assert (== (is-immutable-ref (get-arg 1)) false)))) "
    "(ordinary \"text\") "
    "(ordinary-variadic \"text\" \"other\")",
    EVAL_OK,
    NULL
  );
  clear_program(&interpreter);

  // Variadic declarations with zero named arguments support zero arguments.
  // Both `(call)` and `(apply)` dispatch through the same user-function path.
  run_program(
    &interpreter,
    "(fn \"zero\" (immutable-args) 42) "
    "(assert (== (zero) 42)) "
    "(fn \"size\" (immutable-var-args) argc) "
    "(assert (== (size) 0)) "
    "(assert (== (call \"size\" \"a\" \"b\") 2)) "
    "(fn \"check\" (immutable-args \"text\") (do "
    "  (assert (is-immutable-ref text)) "
    "  (assert (is-immutable-ref (get-arg 0))) "
    "  text)) "
    "(assert (== (apply \"check\" (arr \"hello\")) \"hello\")) "
    "(assert (is-immutable-ref (call \"check\" \"again\")))",
    EVAL_OK,
    NULL
  );
  clear_program(&interpreter);

  // Recursive invocations preserve the correct per-function arguments.
  // The returned view survives a GC that runs inside a function call.
  run_program(
    &interpreter,
    "(fn \"recurse\" (immutable-args \"text\" \"n\") (do "
    "  (assert (is-immutable-ref text)) "
    "  (if (> n 0) (recurse text (- n 1))) "
    "  (assert (is-immutable-ref (get-arg 0))) "
    "  (gc) "
    "  text)) "
    "(var \"saved\" (recurse (str \"live\" \"!\") 4)) "
    "(gc) "
    "(assert (== saved \"live!\"))",
    EVAL_OK,
    NULL
  );
  clear_program(&interpreter);

  // Special values retain their original kinds and types. An argument named
  // `argc` still shadows the implicit numeric argument-count local.
  run_program(
    &interpreter,
    "(fn \"kind\" (immutable-args \"target\") (kind-of \"target\")) "
    "(assert (== (kind _FN_kind) label-kind)) "
    "(fn \"void-type\" (immutable-args \"value\") (type-of value)) "
    "(assert (== (void-type (do "
    "  (local \"temporary\" 1) "
    "  (reset \"temporary\") "
    "  temporary)) void)) "
    "(fn \"shadow\" (immutable-args \"argc\") argc) "
    "(assert (== (shadow 99) 99))",
    EVAL_OK,
    NULL
  );
  clear_program(&interpreter);

  // Read-only named arguments reject attempted in-place writes.
  run_program(
    &interpreter,
    "(fn \"mutate\" (immutable-args \"value\") (str-set \"value\" 0 42)) "
    "(mutate \"text\")",
    EVAL_FAIL,
    "immutable"
  );
  clear_program(&interpreter);
  run_program(
    &interpreter,
    "(fn \"mutate\" (immutable-args \"value\") (arr-set \"value\" 0 42)) "
    "(mutate (arr 1 2))",
    EVAL_FAIL,
    "immutable"
  );
  clear_program(&interpreter);

  // Fixed-arity and required variadic arguments keep the existing checks.
  run_program(
    &interpreter,
    "(fn \"two\" (immutable-args \"x\" \"y\") x) (two 1)",
    EVAL_FAIL,
    "expects exactly 2 arguments"
  );
  clear_program(&interpreter);
  run_program(
    &interpreter,
    "(fn \"one\" (immutable-var-args \"x\") x) (one)",
    EVAL_FAIL,
    "expects at least 1 arguments"
  );
  clear_program(&interpreter);
  run_program(
    &interpreter,
    "(fn \"bad\" (immutable-args 42) 1)",
    EVAL_FAIL,
    "argument names must be string literals"
  );
  clear_program(&interpreter);

  puts("Immutable function argument tests passed");
  return 0;
}
