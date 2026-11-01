#pragma once

#include <stdio.h>

#include "stdlib-macros.h"
#include "symbol.h"

bool populate_stdlib(char (*error_message)[ERR_MSG_SIZE]);

ADD_STDLIB_FN_TYPE_DEF(std_last_val);

ADD_STDLIB_FN_TYPE_DEF(std_type);
ADD_STDLIB_FN_TYPE_DEF(std_type_of);
ADD_STDLIB_FN_TYPE_DEF(std_kind_of);

// Sequences
ADD_STDLIB_FN_TYPE_DEF(std_depth);

ADD_STDLIB_FN_TYPE_DEF(std_exit);

// Assertions
ADD_STDLIB_FN_TYPE_DEF(std_assert);

// Variables
ADD_STDLIB_FN_TYPE_DEF(std_var);
ADD_STDLIB_FN_TYPE_DEF(std_set);

// Strings
ADD_STDLIB_FN_TYPE_DEF(std_str);

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
