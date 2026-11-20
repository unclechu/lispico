#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_depth);
ADD_STDLIB_FN_TYPE_DEF(std_call_depth);
ADD_STDLIB_FN_TYPE_DEF(std_get_arg);
ADD_STDLIB_FN_TYPE_DEF(std_last_val);

#define POPULATE_STDLIB_SEQUENCES() \
  ADD_FN_SYMBOL("last-val", std_last_val); \
  ADD_FN_SYMBOL("depth", std_depth); \
  ADD_FN_SYMBOL("call-depth", std_call_depth); \
  ADD_FN_SYMBOL("get-arg", std_get_arg);
