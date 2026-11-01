#pragma once

#include "stdlib.h"

ADD_STDLIB_FN_TYPE_DEF(std_sin);
ADD_STDLIB_FN_TYPE_DEF(std_cos);
ADD_STDLIB_FN_TYPE_DEF(std_tan);
ADD_STDLIB_FN_TYPE_DEF(std_asin);
ADD_STDLIB_FN_TYPE_DEF(std_acos);
ADD_STDLIB_FN_TYPE_DEF(std_atan);
ADD_STDLIB_FN_TYPE_DEF(std_atan2);
ADD_STDLIB_FN_TYPE_DEF(std_hypot);

bool populate_stdlib_trigonometry(char (*error_message)[ERR_MSG_SIZE]);
