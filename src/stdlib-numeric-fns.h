#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_abs);
ADD_STDLIB_FN_TYPE_DEF(std_min);
ADD_STDLIB_FN_TYPE_DEF(std_max);
ADD_STDLIB_FN_TYPE_DEF(std_clamp);

#define POPULATE_STDLIB_NUMERIC_FNS() \
  ADD_FN_SYMBOL("abs", std_abs); \
  ADD_FN_SYMBOL("min", std_min); \
  ADD_FN_SYMBOL("max", std_max); \
  ADD_FN_SYMBOL("clamp", std_clamp);
