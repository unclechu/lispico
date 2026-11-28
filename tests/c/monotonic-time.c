// Verify the optional POSIX monotonic-time extension, including the clock
// source, elapsed time, argument validation, and repeated evaluations.
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"
#include "../../src/stdlib-time.h"
#include "../../src/symbol.h"

static void configure_time_functions(EvalSettingsInterface *interface) {
  Interpreter *interpreter = interface->interpreter;
  const char *symbol_name;
  POPULATE_STDLIB_TIME();
  return;

  symbol_define_fail:
    interface->report_failure(interface, "Could not register monotonic-time functions");
    return;
}

static EvalOutcome check_program(
  Interpreter *interpreter,
  const char *source,
  EvalOutcomeKind expected,
  const char *expected_failure
) {
  char parse_error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, source, strlen(source), &parse_error));

  EvalOutcome outcome = evaluate(interpreter, configure_time_functions, NULL);
  if (outcome.kind != expected) {
    fprintf(stderr, "Unexpected result for %s: kind %d, message %s\n",
      source, outcome.kind,
      outcome.kind == EVAL_FAIL ? outcome.as.fail_msg : "(no failure)");
  }
  assert(outcome.kind == expected);
  if (expected_failure != NULL) {
    assert(expected == EVAL_FAIL);
    assert(strstr(outcome.as.fail_msg, expected_failure) != NULL);
  }

  clean_evaluation(interpreter);
  clean_symbols(interpreter);
  clean_expressions(interpreter);
  return outcome;
}

// Match the native implementation’s truncation and 32-bit wrap.
static uint32_t clock_milliseconds(struct timespec timestamp) {
  uint64_t milliseconds =
    (uint64_t)timestamp.tv_sec * 1000u + (uint64_t)timestamp.tv_nsec / 1000000u;
  return (uint32_t)milliseconds;
}

int main(void) {
  static Expression expressions[256] = {0};
  static ExpressionAllocation expression_allocations[256] = {0};
  static Symbol symbols[512] = {0};
  static EvalRuntimeAllocation allocations[64] = {0};
  static uint32_t deferred_indices[32] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 256,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 256,
    .symbols = symbols,
    .symbol_capacity = 512,
    .runtime_allocations = allocations,
    .runtime_allocation_capacity = 64,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 32,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  struct timespec first_host_time;
  struct timespec second_host_time;
  assert(clock_gettime(CLOCK_MONOTONIC, &first_host_time) == 0);
  EvalOutcome first = check_program(&interpreter, "(monotonic-ms)", EVAL_OK, NULL);
  assert(clock_gettime(CLOCK_MONOTONIC, &second_host_time) == 0);
  assert(first.result.kind == SYM_VARIABLE);
  assert(first.result.as.value.type == TYPE_U32);

  // The returned timestamp must fall between two readings of the same
  // POSIX clock, including when its 32-bit millisecond value wraps.
  uint32_t host_start = clock_milliseconds(first_host_time);
  uint32_t host_end = clock_milliseconds(second_host_time);
  uint32_t received = first.result.as.value.as.u32;
  assert((uint32_t)(received - host_start) <= (uint32_t)(host_end - host_start));

  struct timespec pause = { .tv_sec = 0, .tv_nsec = 5000000L };
  struct timespec remaining;
  while (nanosleep(&pause, &remaining) != 0) {
    assert(errno == EINTR);
    pause = remaining;
  }
  EvalOutcome second = check_program(&interpreter, "(monotonic-ms)", EVAL_OK, NULL);
  assert(second.result.kind == SYM_VARIABLE);
  assert(second.result.as.value.type == TYPE_U32);
  uint32_t elapsed = second.result.as.value.as.u32 - received;
  assert(elapsed >= 4u && elapsed < 60000u);

  check_program(
    &interpreter,
    "(assert (== (type-of (monotonic-ms)) u32)) (assert (== (gc) 0))",
    EVAL_OK, NULL
  );
  check_program(&interpreter, "(monotonic-ms 1)", EVAL_FAIL, "does not take any arguments");
  check_program(&interpreter, "(monotonic-ms 1 2)", EVAL_FAIL, "does not take any arguments");

  puts("Monotonic time tests passed");
  return 0;
}
