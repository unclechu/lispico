#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_gc);

#define POPULATE_STDLIB_GC() \
  ADD_FN_SYMBOL("gc", std_gc);
