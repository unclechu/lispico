#pragma once

// A set of utilities for arguments validations for stdlib functions implementation

#include <stdio.h>

#include "constants.h"
#include "storage.h"

bool validate_argc(
  Interpreter *interpreter,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  size_t wanted_argc,
  size_t argc
);

// Write a failure message if a symbol is frozen. The caller reports it.
bool validate_symbol_mutable(
  const Symbol *symbol,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE]
);

void report_unexpected_value_type(
  Interpreter *interpreter,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  ValueType type
);

bool validate_two_same_type_variables_inputs(
  Interpreter *interpreter,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  const Datum *args,
  size_t argc
);

bool validate_nonzero_divisor(
  Interpreter *interpreter,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  const Datum *args
);

bool validate_unary_numeric_input(
  Interpreter *interpreter,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  const Datum *args,
  size_t argc
);

Datum std_unary_f32_math(
  Interpreter *interpreter,
  const Datum *args,
  size_t argc,
  const char *fn_name,
  float (*operation)(float)
);
