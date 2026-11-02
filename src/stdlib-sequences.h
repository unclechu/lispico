#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_depth);
ADD_STDLIB_FN_TYPE_DEF(std_last_val);

bool populate_stdlib_sequences(char (*error_message)[ERR_MSG_SIZE]);
