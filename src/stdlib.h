#pragma once

#include <stdio.h>

#include "symbol.h"

bool populate_stdlib(char (*error_message)[ERR_MSG_SIZE]);

// Branching between different kinds of standard library printing functions
typedef enum {
  STD_PRINT = 0,
  STD_PRINTLN,
  STD_EPRINT,
  STD_EPRINTLN,
} StdPrintVariant;

Value std_generic_print(StdPrintVariant variant, const Value *args, size_t argc);
