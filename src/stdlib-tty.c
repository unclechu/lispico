// POSIX terminal support for interactive desktop applications.
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "stdlib-tty.h"
#include "stdlib-validation-utils.h"
#include "stdlib-variables.h"
#include "symbol-error.h"
#include "wipe-memory.h"

// Terminal settings belong to the process, not to an individual `Interpreter`.
// Every access to these variables happens with stdin’s `flockfile()` lock
// held, except at process shutdown when no interpreters should be running.
// `tty-enable` deliberately preserves ISIG, so Ctrl+C still generates SIGINT.
static struct termios tty_saved_attributes;
static bool tty_enabled = false;
static bool tty_exit_handler_registered = false;

// Restore the original mode when normal process shutdown bypasses an explicit
// `(tty-disable)`. Signal handlers are intentionally not installed here.
static void tty_restore_on_exit(void) {
  flockfile(stdin);
  if (tty_enabled) {
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &tty_saved_attributes);
    tty_enabled = false;
  }
  funlockfile(stdin);
}

// (tty-enable) — Switch stdin to noncanonical, no-echo terminal input.
// Leave signal generation active. VMIN=0 and VTIME=0 keep `read()` from
// blocking after `poll()`, even if another consumer races to read a byte.
Datum std_tty_enable(Interpreter *interpreter, const Datum *args, size_t argc) {
  (void)args;
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "tty-enable";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 0, argc)) return return_value;

  flockfile(stdin);
  if (tty_enabled) {
    funlockfile(stdin);
    return return_value;
  }

  if (!isatty(STDIN_FILENO)) {
    funlockfile(stdin);
    snprintf(fail_msg, sizeof(fail_msg), "(%s) standard input is not a terminal", fn_name);
    goto fail;
  }

  struct termios original;
  if (tcgetattr(STDIN_FILENO, &original) != 0) {
    int error_code = errno;
    funlockfile(stdin);
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) failed to read terminal settings: %s",
      fn_name,
      strerror(error_code)
    );
    goto fail;
  }

  // Register cleanup before modifying the terminal. The registration is
  // process-wide, so it only needs to succeed once.
  if (!tty_exit_handler_registered) {
    if (atexit(tty_restore_on_exit) != 0) {
      funlockfile(stdin);
      snprintf(fail_msg, sizeof(fail_msg), "(%s) failed to register terminal cleanup", fn_name);
      goto fail;
    }
    tty_exit_handler_registered = true;
  }

  struct termios modified = original;
  modified.c_lflag &= (tcflag_t)~(ICANON | ECHO | ECHONL);
  modified.c_cc[VMIN] = 0;
  modified.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSANOW, &modified) != 0) {
    int error_code = errno;
    funlockfile(stdin);
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) failed to enable terminal input: %s",
      fn_name,
      strerror(error_code)
    );
    goto fail;
  }

  tty_saved_attributes = original;
  tty_enabled = true;
  funlockfile(stdin);
  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (tty-disable) — Restore stdin’s mode as it was before `(tty-enable)`.
// Calling either operation repeatedly is safe; this is not a nesting counter.
Datum std_tty_disable(Interpreter *interpreter, const Datum *args, size_t argc) {
  (void)args; // Ignore unused warning
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "tty-disable";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 0, argc)) return return_value;

  flockfile(stdin);
  if (!tty_enabled) {
    funlockfile(stdin);
    return return_value;
  }

  if (tcsetattr(STDIN_FILENO, TCSANOW, &tty_saved_attributes) != 0) {
    int error_code = errno;
    funlockfile(stdin);
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) failed to restore terminal settings: %s",
      fn_name,
      strerror(error_code)
    );
    goto fail;
  }

  tty_enabled = false;
  funlockfile(stdin);
  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// Convert a monotonic timestamp to whole milliseconds for timed polling.
static bool monotonic_milliseconds(uint64_t *result) {
  struct timespec timestamp;
  if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) return false;
  *result = (uint64_t)timestamp.tv_sec * 1000u + (uint64_t)timestamp.tv_nsec / 1000000u;
  return true;
}

// (tty-read-byte) — Block until one byte is received, returning it as u32.
// (tty-read-byte TIMEOUT_MS VARIABLE_NAME) — Wait at most TIMEOUT_MS and
// write a received byte into an existing mutable u32 variable. The two
// arguments can be supplied in either order. Return true when a byte was
// written, false on timeout. A timeout never changes the destination.
// Arrow keys and other terminal escape sequences arrive one byte at a time.
Datum std_tty_read_byte(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "tty-read-byte";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};
  Symbol *destination = NULL;
  uint32_t timeout_ms = 0;

  if (argc != 0 && argc != 2) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) expects zero or two arguments, got: %zu",
      fn_name,
      argc
    );
    goto fail;
  }

  if (argc == 2) {
    const Datum *name_arg = NULL;
    const Datum *timeout_arg = NULL;

    // Accept (TIMEOUT_MS NAME) and (NAME TIMEOUT_MS), but not two
    // arguments of the same type or an unsupported value type.
    if (
      args[0].kind == SYM_VARIABLE &&
      args[0].as.value.type == TYPE_U32 &&
      args[1].kind == SYM_VARIABLE &&
      args[1].as.value.type == TYPE_STRING
    ) {
      timeout_arg = &args[0];
      name_arg = &args[1];
    } else if (
      args[0].kind == SYM_VARIABLE &&
      args[0].as.value.type == TYPE_STRING &&
      args[1].kind == SYM_VARIABLE &&
      args[1].as.value.type == TYPE_U32
    ) {
      name_arg = &args[0];
      timeout_arg = &args[1];
    } else {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) expects a u32 timeout and a string variable name",
        fn_name
      );
      goto fail;
    }

    if (!get_variable_name(name_arg, &symbol_name)) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid variable name", fn_name);
      goto fail;
    }

    destination = symbol_lookup_scoped(interpreter, symbol_name);
    if (destination == NULL) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) variable “%s” is not defined",
        fn_name,
        symbol_name
      );
      goto fail;
    }
    if (!validate_symbol_mutable(destination, fn_name, &fail_msg)) goto fail;
    if (
      destination->kind != SYM_VARIABLE ||
      destination->as.variable.type != TYPE_U32
    ) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) variable “%s” must be u32",
        fn_name,
        symbol_name
      );
      goto fail;
    }

    timeout_ms = timeout_arg->as.value.as.u32;
    // Booleans are u32 values. A timeout returns false and leaves the
    // destination untouched; a successful read sets this result to true.
    return_value = (Datum) {
      .kind = SYM_VARIABLE,
      .as.value = { .type = TYPE_U32, .as.u32 = 0u },
    };
  }

  // A missing timeout means blocking input; an explicit zero polls without
  // waiting. Only timed reads need a monotonic deadline.
  bool blocking = argc == 0;
  flockfile(stdin);
  if (!tty_enabled) {
    funlockfile(stdin);
    snprintf(fail_msg, sizeof(fail_msg), "(%s) requires (tty-enable) first", fn_name);
    goto fail;
  }

  uint64_t deadline_ms = 0;
  int remaining_ms = -1;
  if (!blocking) {
    uint64_t started_ms;
    if (!monotonic_milliseconds(&started_ms)) goto monotonic_clock_read_fail;
    deadline_ms = started_ms + (uint64_t)timeout_ms;
    remaining_ms = timeout_ms > INT_MAX ? INT_MAX : (int)timeout_ms;
  }

  while (true) {
    struct pollfd input = {
      .fd = STDIN_FILENO,
      .events = POLLIN,
      .revents = 0,
    };
    int status = poll(&input, 1, remaining_ms);
    if (status > 0) {
      if ((input.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        funlockfile(stdin);
        snprintf(
          fail_msg,
          sizeof(fail_msg),
          "(%s) terminal input closed or became unavailable",
          fn_name
        );
        goto fail;
      }
      if ((input.revents & POLLIN) != 0) {
        unsigned char byte = 0;
        ssize_t amount = read(STDIN_FILENO, &byte, 1);
        if (amount == 1) {
          if (blocking) {
            return_value = (Datum) {
              .kind = SYM_VARIABLE,
              .as.value = { .type = TYPE_U32, .as.u32 = byte },
            };
          } else {
            destination->as.variable.as.u32 = byte;
            return_value.as.value.as.u32 = 1u;
          }
          funlockfile(stdin);
          return return_value;
        }
        if (amount == 0) {
          // With VMIN=VTIME=0, another reader might have consumed the byte
          // between poll and read. Blocking reads must keep waiting.
          if (blocking) continue;
          funlockfile(stdin);
          return return_value;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
          int error_code = errno;
          funlockfile(stdin);
          snprintf(
            fail_msg,
            sizeof(fail_msg),
            "(%s) failed to read terminal input: %s",
            fn_name,
            strerror(error_code)
          );
          goto fail;
        }
      }
    } else if (status < 0 && errno != EINTR) {
      int error_code = errno;
      funlockfile(stdin);
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) failed to poll terminal input: %s",
        fn_name,
        strerror(error_code)
      );
      goto fail;
    }

    if (blocking) continue;
    if (timeout_ms == 0) {
      funlockfile(stdin);
      return return_value;
    }

    uint64_t now_ms;
    if (!monotonic_milliseconds(&now_ms)) goto monotonic_clock_read_fail;
    if (now_ms >= deadline_ms) {
      funlockfile(stdin);
      return return_value;
    }
    uint64_t remaining = deadline_ms - now_ms;
    remaining_ms = remaining > INT_MAX ? INT_MAX : (int)remaining;
  }

  monotonic_clock_read_fail:
    int error_code = errno;
    funlockfile(stdin);
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) failed to read monotonic clock: %s",
      fn_name,
      strerror(error_code)
    );
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (tty-size ARRAY_NAME) — Store columns, rows in the first two elements of
// an existing mutable u32 array. Leave any additional elements unchanged.
Datum std_tty_size(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "tty-size";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;
  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects an array variable name", fn_name);
    goto fail;
  }

  Symbol *symbol = symbol_lookup_scoped(interpreter, symbol_name);
  if (symbol == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) array “%s” is not defined", fn_name, symbol_name);
    goto fail;
  }
  if (symbol->kind != SYM_ARRAY) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) symbol “%s” is not an array", fn_name, symbol_name);
    goto fail;
  }
  if (!validate_symbol_mutable(symbol, fn_name, &fail_msg)) goto fail;
  Array *array = &symbol->as.array;
  if (array->immutable_ref) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) cannot modify an immutable array reference", fn_name);
    goto fail;
  }
  if (array->element_type != TYPE_U32 || array->length < 2 || array->data == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a mutable u32 array of length >= 2", fn_name);
    goto fail;
  }

  // Output is preferable for screen size, but allow redirected stdout when
  // stdin or stderr is connected to a terminal. No active tty mode is needed.
  int fd = -1;
  if (isatty(STDOUT_FILENO)) fd = STDOUT_FILENO;
  else if (isatty(STDIN_FILENO)) fd = STDIN_FILENO;
  else if (isatty(STDERR_FILENO)) fd = STDERR_FILENO;
  if (fd < 0) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) no terminal is connected to standard streams", fn_name);
    goto fail;
  }

  struct winsize dimensions;
  if (ioctl(fd, TIOCGWINSZ, &dimensions) != 0) {
    int error_code = errno;
    snprintf(fail_msg, sizeof(fail_msg), "(%s) failed to read terminal dimensions: %s", fn_name, strerror(error_code));
    goto fail;
  }
  if (dimensions.ws_col == 0 || dimensions.ws_row == 0) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) terminal dimensions are unavailable", fn_name);
    goto fail;
  }

  uint32_t *data = array->data;
  data[0] = dimensions.ws_col;
  data[1] = dimensions.ws_row;
  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
