#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_arr);
ADD_STDLIB_FN_TYPE_DEF(std_arr_new);
ADD_STDLIB_FN_TYPE_DEF(std_arr_get);
ADD_STDLIB_FN_TYPE_DEF(std_arr_set);
ADD_STDLIB_FN_TYPE_DEF(std_arr_size);
ADD_STDLIB_FN_TYPE_DEF(std_arr_type);
ADD_STDLIB_FN_TYPE_DEF(std_arr_slice);
ADD_STDLIB_FN_TYPE_DEF(std_arr_take);
ADD_STDLIB_FN_TYPE_DEF(std_arr_drop);

#define POPULATE_STDLIB_ARRAYS() \
  ADD_FN_SYMBOL("arr", std_arr); \
  ADD_FN_SYMBOL("arr-new", std_arr_new); \
  ADD_FN_SYMBOL("arr-get", std_arr_get); \
  ADD_FN_SYMBOL("arr-set", std_arr_set); \
  ADD_FN_SYMBOL("arr-size", std_arr_size); \
  ADD_FN_SYMBOL("arr-type", std_arr_type); \
  ADD_FN_SYMBOL("arr-slice", std_arr_slice); \
  ADD_FN_SYMBOL("arr-take", std_arr_take); \
  ADD_FN_SYMBOL("arr-drop", std_arr_drop);
