#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_less);
ADD_STDLIB_FN_TYPE_DEF(std_less_equal);
ADD_STDLIB_FN_TYPE_DEF(std_greater);
ADD_STDLIB_FN_TYPE_DEF(std_greater_equal);
ADD_STDLIB_FN_TYPE_DEF(std_equal);
ADD_STDLIB_FN_TYPE_DEF(std_not_equal);

#define POPULATE_STDLIB_COMPARISONS() \
  ADD_FN_SYMBOL("<", std_less); \
  ADD_FN_SYMBOL("<=", std_less_equal); \
  ADD_FN_SYMBOL(">", std_greater); \
  ADD_FN_SYMBOL(">=", std_greater_equal); \
  ADD_FN_SYMBOL("==", std_equal); \
  ADD_FN_SYMBOL("!=", std_not_equal);
