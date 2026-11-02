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

// Labels (registering a label and jumping to it)
ADD_STDLIB_FN_TYPE_DEF(std_label);
ADD_STDLIB_FN_TYPE_DEF(std_goto);
