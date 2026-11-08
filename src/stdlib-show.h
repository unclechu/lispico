#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_show);

#define POPULATE_STDLIB_SHOW() \
  ADD_FN_SYMBOL("show", std_show);
