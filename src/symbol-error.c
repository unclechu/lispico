#include <stdio.h>
#include <string.h>

#include "symbol-error.h"
#include "symbol.h"

void extract_error_message_from_symbol_define_error(
  char (*error_message)[ERR_MSG_SIZE],
  const char *symbol_name
) {
  snprintf(*error_message, sizeof(*error_message), "symbol_define for “%s”: ", symbol_name);
  switch (symbol_define_error) {
    case INCORRECT_INPUT:
      snprintf(
        *error_message + strlen(*error_message),
        sizeof(*error_message) - strlen(*error_message),
        "Received incorrect input"
      );
      break;
    case ALREADY_DEFINED:
      snprintf(
        *error_message + strlen(*error_message),
        sizeof(*error_message) - strlen(*error_message),
        "Name is already defined"
      );
      break;
    case TABLE_IS_FULL:
      snprintf(
        *error_message + strlen(*error_message),
        sizeof(*error_message) - strlen(*error_message),
        "Failed because symbols hash-table is full"
      );
      break;
    default:
      snprintf(
        *error_message + strlen(*error_message),
        sizeof(*error_message) - strlen(*error_message),
        "Undefined error type (should never happen)"
      );
  }
}
