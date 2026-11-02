#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_sleep_s);
ADD_STDLIB_FN_TYPE_DEF(std_sleep_ms);
ADD_STDLIB_FN_TYPE_DEF(std_sleep_us);

#define POPULATE_STDLIB_SLEEP() \
  ADD_FN_SYMBOL("sleep-s", std_sleep_s); \
  ADD_FN_SYMBOL("sleep-ms", std_sleep_ms); \
  ADD_FN_SYMBOL("sleep-us", std_sleep_us);
