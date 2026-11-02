#include "stdlib-assertions.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "wipe-memory.h"
#include "evaluate.h"

// (assert) — Fail evaluation if the condition is false
Datum std_assert(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "assert";

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

  if (argc == 2 && (args[1].kind != SYM_VARIABLE || args[1].as.value.type != TYPE_STRING)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) message must be a string", fn_name);
    goto fail;
  }

  // Assertion was successful
  if (condition) return return_value;

  if (argc == 2) {
    String message = args[1].as.value.as.string;

    if (message.length != 0 && message.data == NULL) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid message string", fn_name);
      goto fail;
    }

    if (message.length != 0) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) assertion failed: %.*s",
        fn_name,
        (int)(message.length < sizeof(fail_msg) ? message.length : sizeof(fail_msg) - 1),
        message.data
      );
      goto fail;
    }
  }

  snprintf(fail_msg, sizeof(fail_msg), "(%s) assertion failed", fn_name);

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
