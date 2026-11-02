#include <string.h>

#include "stdlib-strings.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "evaluate.h"
#include "wipe-memory.h"

// (str) — Concatenate zero or more strings
Datum std_str(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "str";
  size_t length = 0;

  // Validate all arguments and calculate the resulting byte length.
  for (size_t i = 0; i < argc; ++i) {
    if (args[i].kind != SYM_VARIABLE || args[i].as.value.type != TYPE_STRING) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) all arguments must be strings", fn_name);
      goto fail;
    }

    String string = args[i].as.value.as.string;
    if (string.length != 0 && string.data == NULL) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string argument", fn_name);
      goto fail;
    }

    // -1 to account for the '\0' terminator
    if (string.length > SIZE_MAX - length - 1) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) resulting string is too large", fn_name);
      goto fail;
    }

    length += string.length;
  }

  // +1 for the '\0' terminator
  char *data = allocate_runtime_data(length + 1);
  if (data == NULL) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) runtime allocation failed (OOM or allocation capacity reached)",
      fn_name
    );
    goto fail;
  }

  size_t offset = 0;
  for (size_t i = 0; i < argc; ++i) {
    String string = args[i].as.value.as.string;
    if (string.length != 0) {
      memcpy(data + offset, string.data, string.length);
      offset += string.length;
    }
  }

  // Convenience terminator; String.length remains authoritative.
  data[length] = '\0';

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_STRING,
      .as.string = { .data = data, .length = length },
    },
  };

  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// Populate the `symbols` with “standard library” string manipulation functions
bool populate_stdlib_strings(char (*error_message)[ERR_MSG_SIZE]) {
  const char *symbol_name;

  ADD_FN_SYMBOL("str", std_str);

  return true;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(error_message, symbol_name);
    return false;
}
