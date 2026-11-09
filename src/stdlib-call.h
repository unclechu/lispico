#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_call);

#define POPULATE_STDLIB_CALL() \
  ADD_FN_SYMBOL("call", std_call);
