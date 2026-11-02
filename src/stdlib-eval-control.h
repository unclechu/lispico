#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_exit);

#define POPULATE_STDLIB_EVAL_CONTROL() \
  ADD_FN_SYMBOL("exit", std_exit);
