// Exercise the POSIX TTY extension without requiring the test runner to have
// a real terminal. The test owns a pseudoterminal and temporarily uses its
// slave as stdin; the master acts as the keyboard.
#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "../../src/storage.h"
#include "../../src/parse.h"
#include "../../src/evaluate.h"
#include "../../src/stdlib-tty.h"
#include "../../src/symbol.h"

static void configure_tty_functions(EvalSettingsInterface *interface) {
  Interpreter *interpreter = interface->interpreter;
  const char *symbol_name;
  POPULATE_STDLIB_TTY();
  return;

  symbol_define_fail:
    interface->report_failure(interface, "Could not register TTY functions");
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
  EvalOutcome outcome = evaluate(interpreter, configure_tty_functions, NULL);
  if (outcome.kind != expected) {
    fprintf(stderr, "Unexpected result for %s: kind %d, message %s\n",
      source, outcome.kind, outcome.as.fail_msg);
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

static void check_byte(Interpreter *interpreter, const char *expression, uint32_t expected) {
  EvalOutcome outcome = check_program(interpreter, expression, EVAL_OK, NULL);
  assert(outcome.result.kind == SYM_VARIABLE);
  assert(outcome.result.as.value.type == TYPE_U32);
  assert(outcome.result.as.value.as.u32 == expected);
}

int main(void) {
  static Expression expressions[1024] = {0};
  static ExpressionAllocation expression_allocations[1024] = {0};
  static Symbol symbols[1024] = {0};
  static EvalRuntimeAllocation allocations[128] = {0};
  static uint32_t deferred_indices[64] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 1024,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 1024,
    .symbols = symbols,
    .symbol_capacity = 1024,
    .runtime_allocations = allocations,
    .runtime_allocation_capacity = 128,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 64,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // Failures involving wrong arguments occur before any terminal operations.
  check_program(&interpreter, "(tty-enable 1)", EVAL_FAIL, "does not take any arguments");
  check_program(&interpreter, "(tty-disable 1)", EVAL_FAIL, "does not take any arguments");
  check_program(&interpreter, "(tty-read-byte 0 1)", EVAL_FAIL, "expects zero or one argument");
  check_program(&interpreter, "(tty-read-byte -1)", EVAL_FAIL, "timeout must be u32");
  check_program(&interpreter, "(tty-read-byte 1.)", EVAL_FAIL, "timeout must be u32");
  check_program(&interpreter, "(tty-size)", EVAL_FAIL, "expects exactly 1");
  check_program(&interpreter, "(tty-size 123)", EVAL_FAIL, "expects an array variable name");
  check_program(&interpreter, "(tty-size \"missing\")", EVAL_FAIL, "is not defined");
  check_program(&interpreter, "(tty-read-byte 0)", EVAL_FAIL, "requires (tty-enable)");
  check_program(&interpreter, "(tty-read-byte)", EVAL_FAIL, "requires (tty-enable)");

  // A subprocess with no attached TTY must report errors instead of
  // attempting to modify the terminal or returning fictitious dimensions.
  pid_t no_terminal = fork();
  assert(no_terminal >= 0);
  if (no_terminal == 0) {
    int null_fd = open("/dev/null", O_RDWR);
    assert(null_fd >= 0);
    assert(dup2(null_fd, STDIN_FILENO) == STDIN_FILENO);
    assert(dup2(null_fd, STDOUT_FILENO) == STDOUT_FILENO);
    assert(dup2(null_fd, STDERR_FILENO) == STDERR_FILENO);
    close(null_fd);
    check_program(&interpreter, "(tty-enable)", EVAL_FAIL, "not a terminal");
    check_program(&interpreter,
      "(arr-new \"size\" u32 2) (tty-size \"size\")",
      EVAL_FAIL, "no terminal is connected");
    _exit(0);
  }
  int no_terminal_status = 0;
  assert(waitpid(no_terminal, &no_terminal_status, 0) == no_terminal);
  assert(WIFEXITED(no_terminal_status) && WEXITSTATUS(no_terminal_status) == 0);

  // Set up a real pseudoterminal. No interactive device or extra linker
  // library is needed; POSIX openpt functions are in libc on Linux.
  int master = posix_openpt(O_RDWR | O_NOCTTY);
  assert(master >= 0);
  assert(grantpt(master) == 0);
  assert(unlockpt(master) == 0);
  char *slave_name = ptsname(master);
  assert(slave_name != NULL);
  int slave = open(slave_name, O_RDWR | O_NOCTTY);
  assert(slave >= 0);

  struct winsize size = { .ws_row = 27, .ws_col = 83 };
  assert(ioctl(slave, TIOCSWINSZ, &size) == 0);
  struct termios original;
  assert(tcgetattr(slave, &original) == 0);
  int old_stdin = dup(STDIN_FILENO);
  assert(old_stdin >= 0);
  assert(dup2(slave, STDIN_FILENO) == STDIN_FILENO);

  // TTY dimensions do not require enabling input mode. Only the first two
  // elements are changed; the destination may be larger than two.
  check_program(&interpreter,
    "(arr-new \"size\" u32 4) "
    "(arr-set \"size\" 2 777) (arr-set \"size\" 3 888) "
    "(tty-size \"size\") "
    "(assert (== (arr-get size 0) 83)) "
    "(assert (== (arr-get size 1) 27)) "
    "(assert (== (arr-get size 2) 777)) "
    "(assert (== (arr-get size 3) 888)) "
    "(assert (== (gc) 0))",
    EVAL_OK, NULL);
  check_program(&interpreter,
    "(arr-new \"size\" u32 2) (tty-size \"size\") "
    "(assert (== (arr-get size 0) 83))",
    EVAL_OK, NULL);
  check_program(&interpreter,
    "(do (local-arr-new \"size\" u32 2) (tty-size \"size\") "
    "(assert (== (arr-get size 1) 27)))",
    EVAL_OK, NULL);
  check_program(&interpreter,
    "(arr-new \"size\" u32 1) (tty-size \"size\")",
    EVAL_FAIL, "length >= 2");
  check_program(&interpreter,
    "(arr-new \"size\" i32 2) (tty-size \"size\")",
    EVAL_FAIL, "mutable u32 array");
  check_program(&interpreter,
    "(var \"size\" 22) (tty-size \"size\")",
    EVAL_FAIL, "is not an array");
  check_program(&interpreter,
    "(arr-new \"size\" u32 2) (freeze \"size\") (tty-size \"size\")",
    EVAL_FAIL, "frozen");
  check_program(&interpreter,
    "(arr-new \"size\" u32 2) (var \"ref\" (immutable-ref size)) "
    "(tty-size \"ref\")",
    EVAL_FAIL, "immutable array reference");

  // Repeated `(tty-size)` queries observe a window resize without any
  // signal handler, making application-level polling sufficient.
  size.ws_col = 94;
  size.ws_row = 31;
  assert(ioctl(slave, TIOCSWINSZ, &size) == 0);
  check_program(&interpreter,
    "(arr-new \"size\" u32 2) (tty-size \"size\") "
    "(assert (== (arr-get size 0) 94)) (assert (== (arr-get size 1) 31))",
    EVAL_OK, NULL);

  // Input mode is process-global and survives separate interpreter runs.
  check_program(&interpreter, "(tty-enable)", EVAL_OK, NULL);
  check_program(&interpreter, "(tty-enable)", EVAL_OK, NULL);
  struct termios active;
  assert(tcgetattr(slave, &active) == 0);
  assert((active.c_lflag & (ICANON | ECHO)) == 0);
  assert((active.c_lflag & ISIG) == (original.c_lflag & ISIG));
  assert(active.c_cc[VMIN] == 0);
  assert(active.c_cc[VTIME] == 0);

  // A poll without input returns empty, not the numeric NUL byte.
  EvalOutcome empty = check_program(&interpreter, "(tty-read-byte 0)", EVAL_OK, NULL);
  assert(empty.result.kind == SYM_EMPTY);
  empty = check_program(&interpreter, "(tty-read-byte 10)", EVAL_OK, NULL);
  assert(empty.result.kind == SYM_EMPTY);

  assert(write(master, "xy", 2) == 2);
  check_byte(&interpreter, "(tty-read-byte)", 'x');
  check_byte(&interpreter, "(tty-read-byte 0)", 'y');
  unsigned char nul = 0;
  assert(write(master, &nul, 1) == 1);
  check_byte(&interpreter, "(tty-read-byte)", 0);

  // The terminal must not echo the three typed bytes back to the master.
  struct pollfd master_input = { .fd = master, .events = POLLIN };
  assert(poll(&master_input, 1, 0) == 0);

  // Timed waiting receives a byte supplied after the call has begun.
  pid_t writer = fork();
  assert(writer >= 0);
  if (writer == 0) {
    struct timespec delay = { .tv_sec = 0, .tv_nsec = 50000000 };
    nanosleep(&delay, NULL);
    if (write(master, "z", 1) != 1) _exit(1);
    _exit(0);
  }
  EvalOutcome timed = check_program(&interpreter, "(tty-read-byte 500)", EVAL_OK, NULL);
  assert(timed.result.kind == SYM_VARIABLE);
  assert(timed.result.as.value.as.u32 == 'z');
  int writer_status = 0;
  assert(waitpid(writer, &writer_status, 0) == writer);
  assert(WIFEXITED(writer_status) && WEXITSTATUS(writer_status) == 0);

  // With no timeout, a read must wait for input instead of returning empty.
  // The byte is written by another process after the read has started.
  pid_t blocking_writer = fork();
  assert(blocking_writer >= 0);
  if (blocking_writer == 0) {
    struct timespec delay = { .tv_sec = 0, .tv_nsec = 50000000 };
    nanosleep(&delay, NULL);
    if (write(master, "b", 1) != 1) _exit(1);
    _exit(0);
  }
  check_byte(&interpreter, "(tty-read-byte)", 'b');
  int blocking_writer_status = 0;
  assert(waitpid(blocking_writer, &blocking_writer_status, 0) == blocking_writer);
  assert(WIFEXITED(blocking_writer_status) && WEXITSTATUS(blocking_writer_status) == 0);

  check_program(&interpreter, "(tty-disable)", EVAL_OK, NULL);
  check_program(&interpreter, "(tty-disable)", EVAL_OK, NULL);
  struct termios restored;
  assert(tcgetattr(slave, &restored) == 0);
  assert(restored.c_lflag == original.c_lflag);
  assert(restored.c_iflag == original.c_iflag);
  assert(restored.c_oflag == original.c_oflag);
  assert(restored.c_cc[VMIN] == original.c_cc[VMIN]);
  assert(restored.c_cc[VTIME] == original.c_cc[VTIME]);
  check_program(&interpreter, "(tty-read-byte 0)", EVAL_FAIL, "requires (tty-enable)");

  // Process-exit cleanup restores terminal mode even without an explicit
  // `(tty-disable)` in a child that terminates normally.
  pid_t cleanup_child = fork();
  assert(cleanup_child >= 0);
  if (cleanup_child == 0) {
    check_program(&interpreter, "(tty-enable)", EVAL_OK, NULL);
    exit(0);
  }
  int cleanup_status = 0;
  assert(waitpid(cleanup_child, &cleanup_status, 0) == cleanup_child);
  assert(WIFEXITED(cleanup_status) && WEXITSTATUS(cleanup_status) == 0);
  assert(tcgetattr(slave, &restored) == 0);
  assert(restored.c_lflag == original.c_lflag);
  assert(restored.c_iflag == original.c_iflag);

  assert(dup2(old_stdin, STDIN_FILENO) == STDIN_FILENO);
  close(old_stdin);
  close(slave);
  close(master);

  puts("TTY tests passed");
  return 0;
}
