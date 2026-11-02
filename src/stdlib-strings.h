#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_str);

bool populate_stdlib_strings(char (*error_message)[ERR_MSG_SIZE]);
