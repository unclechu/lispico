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

Datum std_generic_print(
  Interpreter *interpreter,
  StdPrintVariant variant,
  const Datum *args,
  size_t argc
);

ADD_STDLIB_FN_TYPE_DEF(std_print);
ADD_STDLIB_FN_TYPE_DEF(std_println);
ADD_STDLIB_FN_TYPE_DEF(std_eprint);
ADD_STDLIB_FN_TYPE_DEF(std_eprintln);

#define POPULATE_STDLIB_PRINT() \
  ADD_FN_SYMBOL("print", std_print); \
  ADD_FN_SYMBOL("println", std_println); \
  ADD_FN_SYMBOL("eprint", std_eprint); \
  ADD_FN_SYMBOL("eprintln", std_eprintln);
