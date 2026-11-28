#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_tty_enable);
ADD_STDLIB_FN_TYPE_DEF(std_tty_disable);
ADD_STDLIB_FN_TYPE_DEF(std_tty_read_byte);
ADD_STDLIB_FN_TYPE_DEF(std_tty_size);

#define POPULATE_STDLIB_TTY() \
  ADD_FN_SYMBOL("tty-enable", std_tty_enable); \
  ADD_FN_SYMBOL("tty-disable", std_tty_disable); \
  ADD_FN_SYMBOL("tty-read-byte", std_tty_read_byte); \
  ADD_FN_SYMBOL("tty-size", std_tty_size);
