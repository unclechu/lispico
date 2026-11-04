#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_var);
ADD_STDLIB_FN_TYPE_DEF(std_set);
ADD_STDLIB_FN_TYPE_DEF(std_override);

bool get_variable_name(const Datum *datum, char (*name)[SYMBOL_NAME_SIZE]);

#define POPULATE_STDLIB_VARIABLES() \
  ADD_FN_SYMBOL("var", std_var); \
  ADD_FN_SYMBOL("set", std_set); \
  ADD_FN_SYMBOL("override", std_override);
