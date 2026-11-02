#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_pow);
ADD_STDLIB_FN_TYPE_DEF(std_sqrt);
ADD_STDLIB_FN_TYPE_DEF(std_log);
ADD_STDLIB_FN_TYPE_DEF(std_log10);
ADD_STDLIB_FN_TYPE_DEF(std_exp);

bool populate_stdlib_pow_n_log(char (*error_message)[ERR_MSG_SIZE]);
