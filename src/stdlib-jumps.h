#pragma once

#include "stdlib.h"

ADD_STDLIB_FN_TYPE_DEF(std_label);
ADD_STDLIB_FN_TYPE_DEF(std_goto);

bool populate_stdlib_jumps(char (*error_message)[ERR_MSG_SIZE]);
