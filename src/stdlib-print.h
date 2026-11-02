#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

// Branching between different kinds of standard library printing functions
typedef enum {
  STD_PRINT = 0,
  STD_PRINTLN,
  STD_EPRINT,
  STD_EPRINTLN,
} StdPrintVariant;

const char* show_StdPrintVariant(StdPrintVariant variant);

// Printing functions
Datum std_generic_print(StdPrintVariant variant, const Datum *args, size_t argc);
ADD_STDLIB_FN_TYPE_DEF(std_print);
ADD_STDLIB_FN_TYPE_DEF(std_println);
ADD_STDLIB_FN_TYPE_DEF(std_eprint);
ADD_STDLIB_FN_TYPE_DEF(std_eprintln);

bool populate_stdlib_print(char (*error_message)[ERR_MSG_SIZE]);
