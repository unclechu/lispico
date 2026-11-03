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
