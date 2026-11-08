#include <limits.h>

#include "stdlib-eval-control.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "wipe-memory.h"
#include "evaluate.h"

// (exit) — Stop program evaluation with an optional result (default: SYM_EMPTY)
Datum std_exit(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "exit";

  if (argc != 1 && argc != 2) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects 1 or 2 arguments", fn_name);
    goto fail;
  }

  if (args[0].kind != SYM_VARIABLE) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) exit code must be a numeric value", fn_name);
    goto fail;
  }

  int exit_code;

  switch (args[0].as.value.type) {
    case TYPE_U32:
      if (args[0].as.value.as.u32 > INT_MAX) goto out_of_range;
      exit_code = (int)args[0].as.value.as.u32;
      break;

    case TYPE_I32:
      exit_code = (int)args[0].as.value.as.i32;
      break;

    default:
      snprintf(fail_msg, sizeof(fail_msg), "(%s) exit code must be u32 or i32", fn_name);
      goto fail;
  }

  if (argc == 2) return_value = args[1];
  report_evaluation_exit(interpreter, exit_code, return_value);
  return return_value;

  out_of_range:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) exit code is out of range for int", fn_name);
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
