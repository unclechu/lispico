#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_var);
ADD_STDLIB_FN_TYPE_DEF(std_set);

bool get_variable_name(const Datum *datum, char (*name)[SYMBOL_NAME_SIZE]);

bool populate_stdlib_variables(char (*error_message)[ERR_MSG_SIZE]);
