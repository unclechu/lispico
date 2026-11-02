#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_depth);
ADD_STDLIB_FN_TYPE_DEF(std_last_val);

#define POPULATE_STDLIB_SEQUENCES() \
  ADD_FN_SYMBOL("last-val", std_last_val); \
  ADD_FN_SYMBOL("depth", std_depth);
