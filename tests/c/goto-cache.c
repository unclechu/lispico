#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"

static EvalOutcome run_source(Interpreter *interpreter, const char *source) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, source, strlen(source), &error));
  return evaluate(interpreter, NULL, NULL);
}

#if EVAL_JUMP_CACHE_ENTRY_COUNT > 0
static size_t cache_entries(const Interpreter *interpreter) {
  size_t count = 0;
  for (size_t i = 0; i < EVAL_JUMP_CACHE_ENTRY_COUNT; ++i)
    if (interpreter->jump_cache[i].valid) count++;
  return count;
}

static bool has_cached_result(const Interpreter *interpreter, uint8_t kind, uint32_t result) {
  for (size_t i = 0; i < EVAL_JUMP_CACHE_ENTRY_COUNT; ++i) {
    const EvalJumpCacheEntry *entry = &interpreter->jump_cache[i];
    if (entry->valid && entry->query_kind == kind && entry->result_index == result)
      return true;
  }
  return false;
}
#endif

int main(void) {
  static Expression expressions[4096] = {0};
  static ExpressionAllocation expression_allocations[1024] = {0};
  static Symbol symbols[1024] = {0};
  static EvalRuntimeAllocation runtime_allocations[256] = {0};
  static uint32_t deferred[64] = {0};
  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 4096,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 1024,
    .symbols = symbols,
    .symbol_capacity = 1024,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 256,
    .deferred_expression_indices = deferred,
    .deferred_expression_capacity = 64,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // Many distinct, repeatedly used jump destinations exercise cache
  // replacement, including two distinct routes in the same bucket.
  char program[16000] = {'\0'};
  size_t used = 0;
  for (uint32_t i = 0; i < 36; ++i) {
    int written = snprintf(
      program + used,
      sizeof(program) - used,
      "(var \"counter_%u\" 0) "
      "(label \"again_%u\") "
      "(set \"counter_%u\" (+ counter_%u 1)) "
      "(if (< counter_%u 4) (goto again_%u)) ",
      i, i, i, i, i, i
    );
    assert(written > 0 && (size_t) written < sizeof(program) - used);
    used += (size_t) written;
  }
  EvalOutcome outcome = run_source(&interpreter, program);
  assert(outcome.kind == EVAL_OK);
  for (uint32_t i = 0; i < 36; ++i) {
    char name[32] = {'\0'};
    snprintf(name, sizeof(name), "counter_%u", i);
    Symbol *symbol = symbol_lookup(&interpreter, name);
    assert(symbol != NULL);
    assert(symbol->kind == SYM_VARIABLE);
    assert(symbol->as.variable.as.u32 == 4);
  }

  #if EVAL_JUMP_CACHE_ENTRY_COUNT > 0
    assert(cache_entries(&interpreter) >= 2);
    bool shared_bucket = false;
    for (size_t i = 0; i < EVAL_JUMP_CACHE_ENTRY_COUNT; i += 2) {
      const EvalJumpCacheEntry *first = &interpreter.jump_cache[i];
      const EvalJumpCacheEntry *second = &interpreter.jump_cache[i + 1];
      if (first->valid && second->valid) {
        assert(
          first->source_index != second->source_index ||
          first->target_index != second->target_index ||
          first->query_kind != second->query_kind
        );
        shared_bucket = true;
      }
    }
    assert(shared_bucket);
  #endif

  // Evaluation cleanup clears cache state without changing the parsed tree.
  clean_evaluation(&interpreter);
  #if EVAL_JUMP_CACHE_ENTRY_COUNT > 0
    assert(cache_entries(&interpreter) == 0);
  #endif
  outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_OK);

  // A nested `(if)` branch is entered without re-evaluating its condition.
  // The evaluator must cache subtree-containment queries as well as routes.
  outcome = run_source(
    &interpreter,
    "(var \"enabled\" true) (var \"counter\" 0) "
    "(if enabled (do (label \"inside\") "
    "(set \"counter\" (+ counter 1)) "
    "(if (== counter 2) (exit 0 counter)))) "
    "(set \"enabled\" false) (goto inside)"
  );
  assert(outcome.kind == EVAL_EXIT);
  assert(outcome.as.exit_code == 0);
  assert(outcome.result.kind == SYM_VARIABLE);
  assert(outcome.result.as.value.as.u32 == 2);
  #if EVAL_JUMP_CACHE_ENTRY_COUNT > 0
    assert(has_cached_result(&interpreter, 2, 1));
  #endif

  // An unreachable destination is a cacheable negative lookup. Its failure
  // location must still identify the `(goto)` call, not the cached query.
  outcome = run_source(&interpreter, "(show (label \"hidden\")) (goto hidden)");
  assert(outcome.kind == EVAL_FAIL);
  assert(strstr(outcome.as.fail_msg, "destination is not reachable") != NULL);
  assert(outcome.failure_expression_index != EXPR_NONE);
  #if EVAL_JUMP_CACHE_ENTRY_COUNT > 0
    assert(has_cached_result(&interpreter, 1, EXPR_NONE));
  #endif

  // Parsing another source invalidates stale expression indices.
  clean_expressions(&interpreter);
  #if EVAL_JUMP_CACHE_ENTRY_COUNT > 0
    assert(cache_entries(&interpreter) == 0);
  #endif
  outcome = run_source(&interpreter, "(var \"result\" 0) (label \"start\") "
    "(set \"result\" (+ result 1)) (if (< result 3) (goto start)) result");
  assert(outcome.kind == EVAL_OK);
  assert(outcome.result.kind == SYM_VARIABLE);
  assert(outcome.result.as.value.as.u32 == 3);

  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);
  puts("Goto route cache tests passed");
  return 0;
}
