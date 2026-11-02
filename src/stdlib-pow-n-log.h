#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_pow);
ADD_STDLIB_FN_TYPE_DEF(std_sqrt);
ADD_STDLIB_FN_TYPE_DEF(std_log);
ADD_STDLIB_FN_TYPE_DEF(std_log10);
ADD_STDLIB_FN_TYPE_DEF(std_exp);

#define POPULATE_STDLIB_POW_N_LOG() \
  ADD_FN_SYMBOL("pow", std_pow); \
  ADD_FN_SYMBOL("sqrt", std_sqrt); \
  ADD_FN_SYMBOL("log", std_log); \
  ADD_FN_SYMBOL("log10", std_log10); \
  ADD_FN_SYMBOL("exp", std_exp);
