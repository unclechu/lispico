#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_sin);
ADD_STDLIB_FN_TYPE_DEF(std_cos);
ADD_STDLIB_FN_TYPE_DEF(std_tan);
ADD_STDLIB_FN_TYPE_DEF(std_asin);
ADD_STDLIB_FN_TYPE_DEF(std_acos);
ADD_STDLIB_FN_TYPE_DEF(std_atan);
ADD_STDLIB_FN_TYPE_DEF(std_atan2);
ADD_STDLIB_FN_TYPE_DEF(std_hypot);

#define POPULATE_STDLIB_TRIGONOMETRY() \
  ADD_FN_SYMBOL("sin", std_sin); \
  ADD_FN_SYMBOL("cos", std_cos); \
  ADD_FN_SYMBOL("tan", std_tan); \
  ADD_FN_SYMBOL("asin", std_asin); \
  ADD_FN_SYMBOL("acos", std_acos); \
  ADD_FN_SYMBOL("atan", std_atan); \
  ADD_FN_SYMBOL("atan2", std_atan2); \
  ADD_FN_SYMBOL("hypot", std_hypot); \
  \
  /* Mathematical constants */ \
  ADD_VAR_SYMBOL("pi", TYPE_F32, f32, 3.14159265358979323846f); \
  ADD_VAR_SYMBOL("e", TYPE_F32, f32, 2.71828182845904523536f);
