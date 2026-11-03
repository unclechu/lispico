#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_arr_new);
ADD_STDLIB_FN_TYPE_DEF(std_arr_get);
ADD_STDLIB_FN_TYPE_DEF(std_arr_set);
ADD_STDLIB_FN_TYPE_DEF(std_arr_size);
ADD_STDLIB_FN_TYPE_DEF(std_arr_type);

#define POPULATE_STDLIB_ARRAYS() \
  ADD_FN_SYMBOL("arr-new", std_arr_new); \
  ADD_FN_SYMBOL("arr-get", std_arr_get); \
  ADD_FN_SYMBOL("arr-set", std_arr_set); \
  ADD_FN_SYMBOL("arr-size", std_arr_size); \
  ADD_FN_SYMBOL("arr-type", std_arr_type);
