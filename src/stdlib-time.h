#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_monotonic_ms);

#define POPULATE_STDLIB_TIME() \
  ADD_FN_SYMBOL("monotonic-ms", std_monotonic_ms);
