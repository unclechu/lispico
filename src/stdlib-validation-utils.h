#pragma once

// A set of utilities for arguments validations for stdlib functions implementation

#include <stdio.h>

#include "constants.h"
#include "symbol-print.h"

bool validate_argc(
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  size_t wanted_argc,
  size_t argc
);

void report_unexpected_value_type(
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  ValueType type
);

bool validate_two_same_type_variables_inputs(
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  const Datum *args,
  size_t argc
);

bool validate_nonzero_divisor(
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  const Datum *args
);

bool validate_unary_numeric_input(
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  const Datum *args,
  size_t argc
);
