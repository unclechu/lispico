#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_str);
ADD_STDLIB_FN_TYPE_DEF(std_str_size);

#define POPULATE_STDLIB_STRINGS() \
  ADD_FN_SYMBOL("str", std_str); \
  ADD_FN_SYMBOL("str-size", std_str_size);
