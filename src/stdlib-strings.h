#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_str);

#define POPULATE_STDLIB_STRINGS() \
  ADD_FN_SYMBOL("str", std_str);
