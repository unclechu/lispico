#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_exit);

bool populate_stdlib_eval_control(char (*error_message)[ERR_MSG_SIZE]);
