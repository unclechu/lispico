#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_label);
ADD_STDLIB_FN_TYPE_DEF(std_goto);

#define POPULATE_STDLIB_JUMPS() \
  ADD_FN_SYMBOL("label", std_label); \
  ADD_FN_SYMBOL("goto", std_goto);
