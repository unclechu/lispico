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

Datum std_last_val(const Datum*, size_t argc);

// Printing functions
Datum std_generic_print(StdPrintVariant variant, const Datum *args, size_t argc);
Datum std_print(const Datum *args, size_t argc);
Datum std_println(const Datum *args, size_t argc);
Datum std_eprint(const Datum *args, size_t argc);
Datum std_eprintln(const Datum *args, size_t argc);

// Arithmetics
Datum std_add(const Datum *args, size_t argc);
