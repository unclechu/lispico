#include "stdlib-validation-utils.h"
#include "wipe-memory.h"
#include "evaluate.h"

bool validate_argc(
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  size_t wanted_argc,
  size_t argc
) {
  if (wanted_argc == argc) return true;
  if (wanted_argc <= 0) {
    snprintf(
      *fail_msg,
      sizeof(*fail_msg),
      "(%s) does not take any arguments, got %zu argument%s",
      fn_name,
      argc,
      (argc == 1 ? "" : "s")
    );
  } else {
    snprintf(
      *fail_msg,
      sizeof(*fail_msg),
      "(%s) expects exactly %zu argument%s, got instead: %zu",
      fn_name,
      wanted_argc,
      (wanted_argc == 1 ? "" : "s"),
      argc
    );
  }
  report_evaluation_failure(*fail_msg);
  wipe_memory(*fail_msg, sizeof(*fail_msg));
  return false;
}

void report_unexpected_value_type(
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  ValueType type
) {
  snprintf(
    *fail_msg,
    sizeof(*fail_msg),
    "(%s) received a value of an unexpected type: %s",
    fn_name,
    show_ValueType(type)
  );
  report_evaluation_failure(*fail_msg);
  wipe_memory(*fail_msg, sizeof(*fail_msg));
}
