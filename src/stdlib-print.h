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
ADD_STDLIB_FN_TYPE_DEF(std_flush_stdout);
ADD_STDLIB_FN_TYPE_DEF(std_flush_stderr);
ADD_STDLIB_FN_TYPE_DEF(std_set_stdout_buffering);
ADD_STDLIB_FN_TYPE_DEF(std_set_stderr_buffering);

#define POPULATE_STDLIB_PRINT() \
  ADD_FN_SYMBOL("print", std_print); \
  ADD_FN_SYMBOL("println", std_println); \
  ADD_FN_SYMBOL("eprint", std_eprint); \
  ADD_FN_SYMBOL("eprintln", std_eprintln); \
  ADD_FN_SYMBOL("flush-stdout", std_flush_stdout); \
  ADD_FN_SYMBOL("flush-stderr", std_flush_stderr); \
  ADD_FN_SYMBOL("set-stdout-buffering", std_set_stdout_buffering); \
  ADD_FN_SYMBOL("set-stderr-buffering", std_set_stderr_buffering);
