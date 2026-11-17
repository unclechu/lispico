#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_var);
ADD_STDLIB_FN_TYPE_DEF(std_local);
ADD_STDLIB_FN_TYPE_DEF(std_get);
ADD_STDLIB_FN_TYPE_DEF(std_reset);
ADD_STDLIB_FN_TYPE_DEF(std_set);
ADD_STDLIB_FN_TYPE_DEF(std_override);
ADD_STDLIB_FN_TYPE_DEF(std_freeze);
ADD_STDLIB_FN_TYPE_DEF(std_is_frozen);

bool get_variable_name(const Datum *datum, char (*name)[SYMBOL_NAME_SIZE]);

#define POPULATE_STDLIB_VARIABLES() \
  ADD_FN_SYMBOL("var", std_var); \
  ADD_FN_SYMBOL("local", std_local); \
  ADD_FN_SYMBOL("get", std_get); \
  ADD_FN_SYMBOL("reset", std_reset); \
  ADD_FN_SYMBOL("set", std_set); \
  ADD_FN_SYMBOL("override", std_override); \
  ADD_FN_SYMBOL("freeze", std_freeze); \
  ADD_FN_SYMBOL("is-frozen", std_is_frozen);
