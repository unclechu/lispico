#pragma once

#include <stdio.h>

#include "stdlib-macros.h"
#include "symbol.h"

bool populate_stdlib(char (*error_message)[ERR_MSG_SIZE]);

ADD_STDLIB_FN_TYPE_DEF(std_last_val);

// Sequences
ADD_STDLIB_FN_TYPE_DEF(std_depth);

ADD_STDLIB_FN_TYPE_DEF(std_exit);

// Assertions
ADD_STDLIB_FN_TYPE_DEF(std_assert);
