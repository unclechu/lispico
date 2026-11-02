#include <math.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include <float.h>

#include "stdlib.h"
#include "stdlib-validation-utils.h"
#include "stdlib-arithmetics.h"
#include "stdlib-numeric-fns.h"
#include "stdlib-pow-n-log.h"
#include "stdlib-trigonometry.h"
#include "stdlib-comparisons.h"
#include "stdlib-strings.h"
#include "stdlib-variables.h"
#include "stdlib-types.h"
#include "stdlib-jumps.h"
#include "symbol-error.h"
#include "symbol-print.h"
#include "wipe-memory.h"
#include "evaluate.h"
#include "number.h"

// (last-val) — Read last evaluation result in a sequence of expressions
Datum std_last_val(const Datum*, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  char fn_name[] = "last-val";

  if (!validate_argc(fn_name, &fail_msg, 0, argc)) return return_value;

  if (!get_last_evaluation_value(&return_value)) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) no previous value is available",
      fn_name
    );
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  }

  return return_value;
}

// (depth) — Get the current `(do)` sequence nesting level
Datum std_depth(const Datum*, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "depth";

  if (!validate_argc(fn_name, &fail_msg, 0, argc)) return return_value;

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = get_current_sequence_depth() },
  };

  return return_value;
}

// (exit) — Stop program evaluation and return the provided result
Datum std_exit(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "exit";

  if (!validate_argc(fn_name, &fail_msg, 2, argc)) return return_value;

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

  return_value = args[1];
  report_evaluation_exit(exit_code, return_value);
  return return_value;

  out_of_range:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) exit code is out of range for int", fn_name);
    goto fail;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

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

// Populate the `symbols` with “standard library” definitions
bool populate_stdlib(char (*error_message)[ERR_MSG_SIZE]) {
  const char *symbol_name;

  // Value accessing
  ADD_FN_SYMBOL("last-val", std_last_val);

  // Sequences
  ADD_FN_SYMBOL("depth", std_depth);

  ADD_FN_SYMBOL("exit", std_exit);

  // Assertions
  ADD_FN_SYMBOL("assert", std_assert);

  // Variables
  if (!populate_stdlib_variables(error_message)) return false;

  // Types
  if (!populate_stdlib_types(error_message)) return false;

  // Strings
  if (!populate_stdlib_strings(error_message)) return false;

  // Arithmetics
  if (!populate_stdlib_arithmetics(error_message)) return false;
  // Basic numeric functions
  if (!populate_stdlib_numeric_fns(error_message)) return false;
  // Powers and logarithms
  if (!populate_stdlib_pow_n_log(error_message)) return false;
  // Trigonometry
  if (!populate_stdlib_trigonometry(error_message)) return false;

  // Comparisons
  if (!populate_stdlib_comparisons(error_message)) return false;

  // Jumps
  if (!populate_stdlib_jumps(error_message)) return false;

  return true;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(error_message, symbol_name);
    return false;
}
