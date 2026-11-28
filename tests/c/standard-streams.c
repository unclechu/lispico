// Validate process-wide stdout/stderr buffering and flushing in independent
// child processes. Each child inherits streams not yet used by Lispico.
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"
#include "../../src/stdlib-print.h"
#include "../../src/symbol.h"

static void configure_print_functions(EvalSettingsInterface *interface) {
  Interpreter *interpreter = interface->interpreter;
  const char *symbol_name;
  POPULATE_STDLIB_PRINT();
  return;

  symbol_define_fail:
    interface->report_failure(interface, "Could not register printing functions");
    return;
}

static void check_program(
  Interpreter *interpreter,
  const char *source,
  EvalOutcomeKind expected,
  const char *expected_failure
) {
  char parse_error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_source(interpreter, source, strlen(source), &parse_error));

  EvalOutcome outcome = evaluate(interpreter, configure_print_functions, NULL);
  if (outcome.kind != expected) {
    fprintf(stderr, "Scenario outcome for %s: kind %d, message %s\n",
      source, (int)outcome.kind,
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
}

static void run_scenario(int scenario) {
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

  switch (scenario) {
    case 0:
      // Both streams can have separately configured exact-size buffers.
      check_program(
        &interpreter,
        "(set-stdout-buffering 7) (set-stderr-buffering 13) "
        "(print \"A\") (eprint \"B\") (flush-stdout) (flush-stderr)",
        EVAL_OK, NULL
      );
      // Configuration persists across interpreter evaluations and cleanup.
      check_program(
        &interpreter,
        "(set-stdout-buffering 5)",
        EVAL_FAIL, "already been used or configured"
      );
      break;
    case 1:
      check_program(
        &interpreter,
        "(set-stdout-buffering \"line\") (set-stderr-buffering \"line\") "
        "(println \"A\") (eprintln \"B\") (flush-stdout) (flush-stderr)",
        EVAL_OK, NULL
      );
      break;
    case 2:
      check_program(
        &interpreter,
        "(set-stdout-buffering 0) (set-stderr-buffering 0) "
        "(print \"A\") (eprint \"B\")",
        EVAL_OK, NULL
      );
      break;
    case 3:
      // Printing even an empty string counts as stream use.
      check_program(&interpreter, "(print \"\")", EVAL_OK, NULL);
      check_program(
        &interpreter,
        "(set-stdout-buffering 16)",
        EVAL_FAIL, "already been used or configured"
      );
      // One stream’s use does not prevent configuring the other.
      check_program(&interpreter, "(set-stderr-buffering 8)", EVAL_OK, NULL);
      break;
    case 4:
      check_program(&interpreter, "(flush-stderr)", EVAL_OK, NULL);
      check_program(
        &interpreter,
        "(set-stderr-buffering \"line\")",
        EVAL_FAIL, "already been used or configured"
      );
      check_program(&interpreter, "(set-stdout-buffering 8)", EVAL_OK, NULL);
      break;
    case 5:
      check_program(&interpreter, "(set-stdout-buffering 8)", EVAL_OK, NULL);
      check_program(
        &interpreter,
        "(set-stdout-buffering 8)",
        EVAL_FAIL, "already been used or configured"
      );
      break;
    case 6:
      // All malformed modes are rejected without configuring the stream.
      check_program(&interpreter, "(set-stdout-buffering)", EVAL_FAIL, "expects exactly 1");
      check_program(&interpreter, "(set-stdout-buffering 2 3)", EVAL_FAIL, "expects exactly 1");
      check_program(&interpreter, "(set-stdout-buffering -1)", EVAL_FAIL, "expects u32 buffer size");
      check_program(&interpreter, "(set-stdout-buffering 1.)", EVAL_FAIL, "expects u32 buffer size");
      check_program(&interpreter, "(set-stdout-buffering \"LINE\")", EVAL_FAIL, "expects u32 buffer size");
      check_program(&interpreter, "(set-stdout-buffering \"lines\")", EVAL_FAIL, "expects u32 buffer size");
      check_program(&interpreter, "(set-stdout-buffering empty)", EVAL_FAIL, "expects u32 buffer size");
      check_program(&interpreter, "(set-stdout-buffering 16)", EVAL_OK, NULL);
      break;
    case 7:
      check_program(&interpreter, "(flush-stdout 1)", EVAL_FAIL, "does not take any arguments");
      check_program(&interpreter, "(flush-stderr 1)", EVAL_FAIL, "does not take any arguments");
      check_program(&interpreter, "(flush-stdout)", EVAL_OK, NULL);
      check_program(&interpreter, "(flush-stderr)", EVAL_OK, NULL);
      break;
    case 8:
      check_program(&interpreter, "(set-stderr-buffering 0)", EVAL_OK, NULL);
      check_program(&interpreter, "(eprint \"A\")", EVAL_OK, NULL);
      check_program(&interpreter, "(set-stderr-buffering 0)", EVAL_FAIL, "already been used or configured");
      break;
    default:
      assert(false);
      break;
  }
}

int main(void) {
  // Fork ensures each scenario begins with fresh process-global stream state.
  for (int scenario = 0; scenario < 9; ++scenario) {
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      run_scenario(scenario);
      // Use exit(), not _exit(), to test libc’s final buffered stream flush.
      return 0;
    }

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
  }
  return 0;
}
