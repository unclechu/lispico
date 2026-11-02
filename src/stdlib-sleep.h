#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_sleep_s);
ADD_STDLIB_FN_TYPE_DEF(std_sleep_ms);
ADD_STDLIB_FN_TYPE_DEF(std_sleep_us);

bool populate_stdlib_sleep(char (*error_message)[ERR_MSG_SIZE]);
