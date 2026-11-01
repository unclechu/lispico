#pragma once

#include "stdlib.h"

ADD_STDLIB_FN_TYPE_DEF(std_less);
ADD_STDLIB_FN_TYPE_DEF(std_less_equal);
ADD_STDLIB_FN_TYPE_DEF(std_greater);
ADD_STDLIB_FN_TYPE_DEF(std_greater_equal);
ADD_STDLIB_FN_TYPE_DEF(std_equal);
ADD_STDLIB_FN_TYPE_DEF(std_not_equal);

bool populate_stdlib_comparisons(char (*error_message)[ERR_MSG_SIZE]);
