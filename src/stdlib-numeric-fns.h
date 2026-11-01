#pragma once

#include "stdlib.h"

ADD_STDLIB_FN_TYPE_DEF(std_abs);
ADD_STDLIB_FN_TYPE_DEF(std_min);
ADD_STDLIB_FN_TYPE_DEF(std_max);
ADD_STDLIB_FN_TYPE_DEF(std_clamp);

bool populate_stdlib_numeric_fns(char (*error_message)[ERR_MSG_SIZE]);
