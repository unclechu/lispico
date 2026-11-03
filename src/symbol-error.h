#pragma once

#include "constants.h"
#include "storage.h"

void extract_error_message_from_symbol_define_error(
  Interpreter *interpreter,
  char (*error_message)[ERR_MSG_SIZE],
  const char *symbol_name
);
