// Enable POSIX stream locks for safe use by parallel interpreter instances.
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "stdlib-print.h"
#include "stdlib-validation-utils.h"
#include "symbol-print.h"
#include "evaluate.h"
#include "wipe-memory.h"
#include "symbol-error.h"

// Buffering belongs to the C process streams, not to an Interpreter. Access
// this state only while holding the corresponding `FILE` lock via `flockfile()`.
// That lock also serializes configuration with Lispico’s writes and flushes.
// A supplied `setvbuf()` buffer must remain alive as long as its stream can use
// it, including stdio’s automatic flush when the process terminates.
typedef struct {
  bool used;
  bool configured;
  char *buffer;
} StandardStreamState;

static StandardStreamState standard_stdout_state = {0};
static StandardStreamState standard_stderr_state = {0};

static StandardStreamState* standard_stream_state(FILE *stream) {
  return stream == stdout ? &standard_stdout_state : &standard_stderr_state;
}

// (flush-stdout) / (flush-stderr) — Flush a standard output stream.
static Datum std_flush_stream(
  Interpreter *interpreter,
  const Datum *args,
  size_t argc,
  FILE *stream,
  const char *fn_name
) {
  (void)args;
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  if (!validate_argc(interpreter, fn_name, &fail_msg, 0, argc)) return return_value;

  flockfile(stream);
  standard_stream_state(stream)->used = true;
  int status = fflush(stream);
  int error_code = errno;
  funlockfile(stream);

  if (status == 0) return return_value;

  snprintf(
    fail_msg,
    sizeof(fail_msg),
    "(%s) failed to flush stream: %s",
    fn_name,
    strerror(error_code)
  );
  goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (set-stdout-buffering MODE) / (set-stderr-buffering MODE)
// MODE is u32: 0 disables buffering, N > 0 supplies exactly N bytes of
// backing storage for full buffering. The string "line" enables line mode.
// The requested buffer is host-owned (not GC-managed), for process lifetime.
// C’s `setvbuf()` must be called before any other operation on the stream, and
// it may not be used to reconfigure a previously configured stream.
static Datum std_set_stream_buffering(
  Interpreter *interpreter,
  const Datum *args,
  size_t argc,
  FILE *stream,
  const char *fn_name
) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  int mode = _IONBF; // No buffering
  size_t buffer_size = 0;
  char *buffer = NULL;

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;

  if (args[0].kind != SYM_VARIABLE) goto invalid_mode;
  const Value *value = &args[0].as.value;
  if (value->type == TYPE_U32) {
    if (value->as.u32 != 0) {
      mode = _IOFBF; // Full buffering (determined by the buffer size)
      buffer_size = (size_t)value->as.u32;
    }
  } else if (
    value->type == TYPE_STRING &&
    value->as.string.length == 4 &&
    value->as.string.data != NULL &&
    memcmp(value->as.string.data, "line", value->as.string.length) == 0
  ) {
    mode = _IOLBF; // Line buffering
  } else {
    goto invalid_mode;
  }

  // Do not allocate a buffer at all in the unbuffered or line modes.
  // Allocating first avoids calling `setvbuf()` if the requested capacity
  // cannot actually be reserved. The lock protects state across interpreters.
  flockfile(stream);
  StandardStreamState *state = standard_stream_state(stream);
  if (state->used || state->configured) {
    funlockfile(stream);
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) stream has already been used or configured",
      fn_name
    );
    goto fail;
  }

  if (mode == _IOFBF) {
    buffer = malloc(buffer_size);
    if (buffer == NULL) {
      funlockfile(stream);
      snprintf(fail_msg, sizeof(fail_msg), "(%s) failed to allocate stream buffer", fn_name);
      goto fail;
    }
  }

  // Supply our own N-byte buffer rather than a NULL buffer, for which the
  // C library may choose a different capacity. It may still flush early.
  // Preserve the buffer until process termination, even after the
  // interpreter that configured the stream has been destroyed.
  int status = setvbuf(stream, buffer, mode, buffer_size);
  if (status != 0) {
    free(buffer);
    funlockfile(stream);
    snprintf(fail_msg, sizeof(fail_msg), "(%s) failed to configure stream buffering", fn_name);
    goto fail;
  }

  state->buffer = buffer;
  state->configured = true;
  funlockfile(stream);
  return return_value;

  invalid_mode:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) expects u32 buffer size (0 disables buffering) or string “line”",
      fn_name
    );
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

Datum std_flush_stdout(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_flush_stream(interpreter, args, argc, stdout, "flush-stdout");
}

Datum std_flush_stderr(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_flush_stream(interpreter, args, argc, stderr, "flush-stderr");
}

Datum std_set_stdout_buffering(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_set_stream_buffering(interpreter, args, argc, stdout, "set-stdout-buffering");
}

Datum std_set_stderr_buffering(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_set_stream_buffering(interpreter, args, argc, stderr, "set-stderr-buffering");
}

// Get a string representation of `show_StdPrintVariant` value
const char* show_StdPrintVariant(StdPrintVariant variant) {
  switch (variant) {
    case STD_PRINT: return "print";
    case STD_PRINTLN: return "println";
    case STD_EPRINT: return "eprint";
    case STD_EPRINTLN: return "eprintln";
    default: return "( ! Unexpected StdPrintVariant for show_StdPrintVariant ! )";
  }
}

// Generic standard library printing implementation
Datum std_generic_print(
  Interpreter *interpreter,
  StdPrintVariant variant,
  const Datum *args,
  size_t argc
) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char *fn_name = show_StdPrintVariant(variant);

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) {
    return return_value;
  } else if (args[0].kind != SYM_VARIABLE) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) argument must be a varaible, got instead: %s",
      fn_name,
      show_SymbolKind(args[0].kind)
    );
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  } else if (args[0].as.value.type != TYPE_STRING) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "%s argument must be a string, got instead: %s",
      fn_name,
      show_ValueType(args[0].as.value.type)
    );
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  }

  FILE *stream =
    (variant == STD_EPRINT || variant == STD_EPRINTLN)
      ? stderr
      : stdout;

  // Serialize output with other interpreters’ printing and buffering
  // configuration. The state belongs to stdout/stderr, never an Interpreter.
  flockfile(stream);
  standard_stream_state(stream)->used = true;
  fwrite(
    args[0].as.value.as.string.data,
    sizeof(char),
    args[0].as.value.as.string.length,
    stream
  );

  if (variant == STD_PRINTLN || variant == STD_EPRINTLN) fputc('\n', stream);
  funlockfile(stream);

  return return_value;
}

// (print)
Datum std_print(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_generic_print(interpreter, STD_PRINT, args, argc);
}

// (println)
Datum std_println(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_generic_print(interpreter, STD_PRINTLN, args, argc);
}

// (eprint)
Datum std_eprint(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_generic_print(interpreter, STD_EPRINT, args, argc);
}

// (eprintln)
Datum std_eprintln(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_generic_print(interpreter, STD_EPRINTLN, args, argc);
}
