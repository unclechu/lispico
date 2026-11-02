#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_add);
ADD_STDLIB_FN_TYPE_DEF(std_subtract);
ADD_STDLIB_FN_TYPE_DEF(std_mul);
ADD_STDLIB_FN_TYPE_DEF(std_divide);
ADD_STDLIB_FN_TYPE_DEF(std_mod);
ADD_STDLIB_FN_TYPE_DEF(std_div);
ADD_STDLIB_FN_TYPE_DEF(std_floor);
ADD_STDLIB_FN_TYPE_DEF(std_ceiling);
ADD_STDLIB_FN_TYPE_DEF(std_round);
ADD_STDLIB_FN_TYPE_DEF(std_trunc);

#define POPULATE_STDLIB_ARITHMETICS() \
  ADD_FN_SYMBOL("+", std_add); \
  ADD_FN_SYMBOL("-", std_subtract); \
  ADD_FN_SYMBOL("*", std_mul); \
  ADD_FN_SYMBOL("/", std_divide); \
  ADD_FN_SYMBOL("mod", std_mod); \
  ADD_FN_SYMBOL("div", std_div); \
  ADD_FN_SYMBOL("floor", std_floor); \
  ADD_FN_SYMBOL("ceiling", std_ceiling); \
  ADD_FN_SYMBOL("round", std_round); \
  ADD_FN_SYMBOL("trunc", std_trunc);
