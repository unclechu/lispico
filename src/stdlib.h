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

#define ADD_STDLIB_FN_TYPE_DEF(name) Datum name(const Datum*, size_t argc)

ADD_STDLIB_FN_TYPE_DEF(std_last_val);

ADD_STDLIB_FN_TYPE_DEF(std_type);

// Printing functions
Datum std_generic_print(StdPrintVariant variant, const Datum *args, size_t argc);
ADD_STDLIB_FN_TYPE_DEF(std_print);
ADD_STDLIB_FN_TYPE_DEF(std_println);
ADD_STDLIB_FN_TYPE_DEF(std_eprint);
ADD_STDLIB_FN_TYPE_DEF(std_eprintln);

ADD_STDLIB_FN_TYPE_DEF(std_exit);

// Variables
ADD_STDLIB_FN_TYPE_DEF(std_var);
ADD_STDLIB_FN_TYPE_DEF(std_set);

// Arithmetics
ADD_STDLIB_FN_TYPE_DEF(std_add);
ADD_STDLIB_FN_TYPE_DEF(std_subtract);
ADD_STDLIB_FN_TYPE_DEF(std_mul);
ADD_STDLIB_FN_TYPE_DEF(std_divide);
ADD_STDLIB_FN_TYPE_DEF(std_mod);
ADD_STDLIB_FN_TYPE_DEF(std_div);
ADD_STDLIB_FN_TYPE_DEF(std_floor);
ADD_STDLIB_FN_TYPE_DEF(std_ceiling);
ADD_STDLIB_FN_TYPE_DEF(std_round);
ADD_STDLIB_FN_TYPE_DEF(std_trunc);

// Comparisons
ADD_STDLIB_FN_TYPE_DEF(std_less);
ADD_STDLIB_FN_TYPE_DEF(std_less_equal);
ADD_STDLIB_FN_TYPE_DEF(std_greater);
ADD_STDLIB_FN_TYPE_DEF(std_greater_equal);
ADD_STDLIB_FN_TYPE_DEF(std_equal);
ADD_STDLIB_FN_TYPE_DEF(std_not_equal);

// Labels (registering a label and jumping to it)
ADD_STDLIB_FN_TYPE_DEF(std_label);
ADD_STDLIB_FN_TYPE_DEF(std_goto);
