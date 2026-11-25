#include <stdio.h>

#include "stdlib-assertions.h"
#include "storage.h"
#include "wipe-memory.h"
#include "evaluate.h"

// Shared implementation for conditional `(assert)` and unconditional `(fail)`.
static Datum trigger_eval_failure(
  Interpreter *interpreter,
  const Datum *args,
  size_t argc,
  const char *fn_name,
  bool unconditional
) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  size_t message_index = unconditional ? 0 : 1;
  bool should_fail = true;

  if (unconditional) {
    if (argc > 1) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) expects 0 or 1 arguments", fn_name);
      goto fail;
    }
  } else {
    if (argc != 1 && argc != 2) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) expects 1 or 2 arguments", fn_name);
      goto fail;
    }

    if (args[0].kind != SYM_VARIABLE) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) condition must be a numeric value", fn_name);
      goto fail;
    }

    bool condition;
    switch (args[0].as.value.type) {
      case TYPE_U32: condition = args[0].as.value.as.u32 != 0; break;
      case TYPE_I32: condition = args[0].as.value.as.i32 != 0; break;
      case TYPE_F32: condition = args[0].as.value.as.f32 != 0.0f; break;
      default:
        snprintf(fail_msg, sizeof(fail_msg), "(%s) condition must be a numeric value", fn_name);
        goto fail;
    }

    should_fail = !condition;
  }

  // `(assert)` also checks an optional message on success, as before.
  if (
    argc > message_index &&
    (
      args[message_index].kind != SYM_VARIABLE ||
      args[message_index].as.value.type != TYPE_STRING
    )
  ) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) message must be a string", fn_name);
    goto fail;
  }

  if (!should_fail) return return_value;

  const char *description = unconditional ? "evaluation failed" : "assertion failed";
  if (argc > message_index) {
    String message = args[message_index].as.value.as.string;

    if (message.length != 0 && message.data == NULL) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid message string", fn_name);
      goto fail;
    }

    if (message.length != 0) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) %s: %.*s",
        fn_name,
        description,
        (int)(message.length < sizeof(fail_msg) ? message.length : sizeof(fail_msg) - 1),
        message.data
      );
      goto fail;
    }
  }

  snprintf(fail_msg, sizeof(fail_msg), "(%s) %s", fn_name, description);
  goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (assert) — Fail evaluation when a numeric condition is false.
Datum std_assert(Interpreter *interpreter, const Datum *args, size_t argc) {
  return trigger_eval_failure(interpreter, args, argc, "assert", false);
}

// (fail) — Unconditionally fail evaluation, optionally with a string message.
Datum std_fail(Interpreter *interpreter, const Datum *args, size_t argc) {
  return trigger_eval_failure(interpreter, args, argc, "fail", true);
}
