#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_env_var_has);
ADD_STDLIB_FN_TYPE_DEF(std_env_var_read);

#define POPULATE_STDLIB_ENV_VAR() \
  ADD_FN_SYMBOL("env-var-has", std_env_var_has); \
  ADD_FN_SYMBOL("env-var-read", std_env_var_read);
