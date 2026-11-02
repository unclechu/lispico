#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

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

bool populate_stdlib_arithmetics(char (*error_message)[ERR_MSG_SIZE]);
