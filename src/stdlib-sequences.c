#include <inttypes.h>

#include "stdlib-sequences.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "wipe-memory.h"
#include "evaluate.h"

// (last-val) — Read last evaluation result in a sequence of expressions
Datum std_last_val(Interpreter *interpreter, const Datum*, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  char fn_name[] = "last-val";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 0, argc)) return return_value;

  if (!get_last_evaluation_value(interpreter, &return_value)) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) no previous value is available",
      fn_name
    );
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  }

  return return_value;
}

// (depth) — Get the current `(do)` sequence nesting level
Datum std_depth(Interpreter *interpreter, const Datum*, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "depth";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 0, argc)) return return_value;

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = get_current_sequence_depth(interpreter) },
  };

  return return_value;
}

// (get-arg INDEX) — Read a zero-based argument of the active pseudo-function.
Datum std_get_arg(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "get-arg";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;

  if (interpreter->function_call == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) requires an active (fn) call", fn_name);
    goto fail;
  }

  if (args[0].kind != SYM_VARIABLE || args[0].as.value.type != TYPE_U32) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) index must be u32", fn_name);
    goto fail;
  }

  uint32_t index = args[0].as.value.as.u32;
  if (index >= interpreter->function_call->argument_count) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) argument index %" PRIu32 " is out of bounds (argument count is %zu)",
      fn_name,
      index,
      interpreter->function_call->argument_count
    );
    goto fail;
  }

  return interpreter->function_call->arguments[index];

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
