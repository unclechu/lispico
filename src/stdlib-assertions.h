#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_assert);

#define POPULATE_STDLIB_ASSERTIONS() \
  ADD_FN_SYMBOL("assert", std_assert);
