// Stress-test parsing, evaluation, and cleanup in independent pthreads.
// Build with -pthread; run with ThreadSanitizer for race detection.

#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"
#include "../../src/symbol.h"

#ifndef TEST_THREAD_COUNT
  #define TEST_THREAD_COUNT 8
#endif
#ifndef TEST_ITERATIONS
  #define TEST_ITERATIONS 3000
#endif

#if TEST_THREAD_COUNT < 2 || TEST_ITERATIONS < 1
  #error TEST_THREAD_COUNT must be >= 2 and TEST_ITERATIONS must be >= 1
#endif
#ifdef NDEBUG
  #error This test requires assertions to be enabled
#endif

typedef struct {
  Expression expressions[256];
  ExpressionAllocation expression_allocations[128];
  Symbol symbols[256];
  EvalRuntimeAllocation runtime_allocations[16];
  uint32_t deferred_expression_indices[16];
} TestStorage;

// The gate only synchronizes thread startup, not interpreter operations.
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  unsigned ready;
  bool start;
} StartGate;

typedef struct {
  unsigned thread_id;
  unsigned completed;
  StartGate *gate;
  TestStorage buffers;
} Worker;

static void start_together(StartGate *gate) {
  assert(pthread_mutex_lock(&gate->mutex) == 0);
  gate->ready++;
  if (gate->ready == TEST_THREAD_COUNT)
    assert(pthread_cond_broadcast(&gate->condition) == 0);
  while (!gate->start)
    assert(pthread_cond_wait(&gate->condition, &gate->mutex) == 0);
  assert(pthread_mutex_unlock(&gate->mutex) == 0);
}

static void *run_worker(void *argument) {
  Worker *worker = argument;
  Interpreter interpreter = {0};
  TestStorage *buffers = &worker->buffers;
  InterpreterStorage storage = {
    .expressions = buffers->expressions,
    .expression_capacity = 256,
    .expression_allocations = buffers->expression_allocations,
    .expression_allocation_capacity = 128,
    .symbols = buffers->symbols,
    .symbol_capacity = 256,
    .runtime_allocations = buffers->runtime_allocations,
    .runtime_allocation_capacity = 16,
    .deferred_expression_indices = buffers->deferred_expression_indices,
    .deferred_expression_capacity = 16,
  };
  char source[512];
  char error[ERR_MSG_SIZE] = {'\0'};

  // All threads begin configuring, parsing, and evaluating concurrently.
  start_together(worker->gate);
  assert(configure_interpreter_storage(&interpreter, &storage));

  for (unsigned iteration = 0; iteration < TEST_ITERATIONS; ++iteration) {
    // The same symbol names deliberately hold different values in each thread.
    uint32_t seed = 1000u + worker->thread_id * 100000u + iteration % 10000u;
    int written = snprintf(
      source, sizeof(source),
      "(var \"value\" %" PRIu32 ") "
      "(var \"fraction\" 1.5) "
      "(set \"fraction\" (* fraction 2.0)) "
      "(var \"message\" (str-copy \"abcdef\" 3)) "
      "(str-set \"message\" 2 (str-char \"!\")) "
      "(str-new \"scratch\" 6) "
      "(str-set \"scratch\" \"XY\") "
      "(do (defer (set \"value\" (+ value 1))) "
      "    (set \"value\" (+ value 2))) "
      "(+ value (str-len message) (str-len scratch))",
      seed
    );
    assert(written > 0 && (size_t)written < sizeof(source));

    assert(parse_source(&interpreter, source, (size_t)written, &error));
    EvalOutcome result = evaluate(&interpreter, NULL, NULL);
    assert(result.kind == EVAL_OK);
    assert(result.result.kind == SYM_VARIABLE);
    assert(result.result.as.value.type == TYPE_U32);
    assert(result.result.as.value.as.u32 == seed + 8u);

    const Symbol *value = symbol_lookup(&interpreter, "value");
    const Symbol *fraction = symbol_lookup(&interpreter, "fraction");
    const Symbol *message = symbol_lookup(&interpreter, "message");
    const Symbol *scratch = symbol_lookup(&interpreter, "scratch");
    assert(value != NULL && value->kind == SYM_VARIABLE);
    assert(value->as.variable.type == TYPE_U32);
    assert(value->as.variable.as.u32 == seed + 3u);
    assert(fraction != NULL && fraction->kind == SYM_VARIABLE);
    assert(fraction->as.variable.type == TYPE_F32);
    assert(fraction->as.variable.as.f32 == 3.0f);
    assert(message != NULL && message->kind == SYM_VARIABLE);
    assert(message->as.variable.type == TYPE_STRING);
    assert(message->as.variable.as.string.length == 3);
    assert(memcmp(message->as.variable.as.string.data, "ab!", 3) == 0);
    assert(scratch != NULL && scratch->kind == SYM_VARIABLE);
    assert(scratch->as.variable.type == TYPE_STRING);
    assert(scratch->as.variable.as.string.length == 6);
    assert(memcmp(scratch->as.variable.as.string.data, "XY\0\0\0\0", 6) == 0);
    assert(interpreter.runtime_allocation_count >= 3);

    // Failures and subsequent recovery must also remain instance-local.
    if (iteration % 23u == 0) {
      const char *bad = "(str-copy \"abc\" 4)";
      assert(parse_source(&interpreter, bad, strlen(bad), &error));
      EvalOutcome failure = evaluate(&interpreter, NULL, NULL);
      assert(failure.kind == EVAL_FAIL);
    }
    if (iteration % 41u == 0) {
      const char *bad = "(var \"unfinished\" 123";
      assert(!parse_source(&interpreter, bad, strlen(bad), &error));
      assert(interpreter.expression_count == 0);
      assert(error[0] != '\0');
    }

    worker->completed++;
  }

  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);
  assert(interpreter.runtime_allocation_count == 0);
  assert(interpreter.expression_allocation_count == 0);
  assert(interpreter.expression_count == 0);
  assert(interpreter.symbol_count == 0);
  return NULL;
}

int main(void) {
  Worker *workers = calloc(TEST_THREAD_COUNT, sizeof(*workers));
  assert(workers != NULL);

  StartGate gate = {0};
  assert(pthread_mutex_init(&gate.mutex, NULL) == 0);
  assert(pthread_cond_init(&gate.condition, NULL) == 0);

  pthread_t threads[TEST_THREAD_COUNT];
  for (unsigned i = 0; i < TEST_THREAD_COUNT; ++i) {
    workers[i].thread_id = i;
    workers[i].gate = &gate;
    assert(pthread_create(&threads[i], NULL, run_worker, &workers[i]) == 0);
  }

  // Release workers only when all threads are ready to run.
  assert(pthread_mutex_lock(&gate.mutex) == 0);
  while (gate.ready != TEST_THREAD_COUNT)
    assert(pthread_cond_wait(&gate.condition, &gate.mutex) == 0);
  gate.start = true;
  assert(pthread_cond_broadcast(&gate.condition) == 0);
  assert(pthread_mutex_unlock(&gate.mutex) == 0);

  for (unsigned i = 0; i < TEST_THREAD_COUNT; ++i) {
    void *result = NULL;
    assert(pthread_join(threads[i], &result) == 0);
    assert(result == NULL);
    assert(workers[i].completed == TEST_ITERATIONS);
  }

  assert(pthread_cond_destroy(&gate.condition) == 0);
  assert(pthread_mutex_destroy(&gate.mutex) == 0);
  free(workers);

  printf(
    "Interpreter multi-thread tests passed (%u threads, %u iterations each)\n",
    (unsigned)TEST_THREAD_COUNT,
    (unsigned)TEST_ITERATIONS
  );
  return 0;
}
