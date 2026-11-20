#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"

static void check_local_state(const Interpreter *interpreter) {
  for (uint32_t i = 0; i < interpreter->storage.symbol_capacity; ++i) {
    const Symbol *symbol = &interpreter->storage.symbols[i];
    if (strncmp(symbol->name, "_LOCAL_", sizeof("_LOCAL_") - 1) != 0) continue;

    assert(symbol->kind == SYM_VARIABLE);
    assert(symbol->as.variable.type == TYPE_VOID);
    assert(symbol->local_scope_owner == NULL);
    assert(symbol->next_local == NULL);
    assert(!symbol->frozen);

    // A released local must not retain stale array or string pointers in
    // the unused part of its symbol value.
    const unsigned char *data = (const unsigned char *)&symbol->as;
    for (size_t j = 0; j < sizeof(symbol->as); ++j) assert(data[j] == 0);
  }
}

static void check_program(
  Interpreter *interpreter,
  const char *source,
  EvalOutcomeKind expected_kind
) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, source, strlen(source), &error));

  EvalOutcome outcome = evaluate(interpreter, NULL, NULL);
  assert(outcome.kind == expected_kind);
  assert(interpreter->gc_roots == NULL);
  assert(interpreter->local_scope == NULL);
  assert(interpreter->active_local_scope == NULL);
  assert(interpreter->function_call == NULL);
  assert(interpreter->current_sequence_depth == 0);
  check_local_state(interpreter);
}

int main(void) {
  static Expression expressions[512] = {0};
  static ExpressionAllocation expression_allocations[256] = {0};
  static Symbol symbols[512] = {0};
  static EvalRuntimeAllocation runtime_allocations[256] = {0};
  static uint32_t deferred_indices[64] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 512,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 256,
    .symbols = symbols,
    .symbol_capacity = 512,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 256,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 64,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // Inner function frames, nested `(do)` scopes, and GC roots are removed
  // without wiping the caller’s live data or the returned string allocation.
  check_program(
    &interpreter,
    "(fn \"inner\" (args \"text\") (do "
    "  (local \"scratch\" (str text \"!\")) "
    "  (defer (gc)) "
    "  scratch)) "
    "(fn \"outer\" (args \"text\") (do "
    "  (local \"copy\" (inner text)) "
    "  (gc) "
    "  copy)) "
    "(var \"message\" \"hello\") "
    "(assert (== (outer message) \"hello!\")) "
    "(assert (== message \"hello\"))",
    EVAL_OK
  );

  // Failed evaluations still unwind function frames, deferred expressions,
  // argument GC roots, and any local values registered in their scopes.
  check_program(
    &interpreter,
    "(fn \"fail\" (args \"text\") (do "
    "  (local \"scratch\" (arr text \"other\")) "
    "  (defer (gc)) "
    "  unknown)) "
    "(fail \"hello\")",
    EVAL_FAIL
  );

  clean_evaluation(&interpreter);
  assert(interpreter.gc_roots == NULL);
  assert(interpreter.local_scope == NULL);
  assert(interpreter.active_local_scope == NULL);
  assert(interpreter.function_call == NULL);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);

  puts("Evaluator stack wiping tests passed");
  return 0;
}
