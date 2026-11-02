#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_type);
ADD_STDLIB_FN_TYPE_DEF(std_type_of);
ADD_STDLIB_FN_TYPE_DEF(std_kind_of);

bool populate_stdlib_types(char (*error_message)[ERR_MSG_SIZE]);
