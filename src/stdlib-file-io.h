#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_read_file);
ADD_STDLIB_FN_TYPE_DEF(std_write_file);

#define POPULATE_STDLIB_FILE_IO() \
  ADD_FN_SYMBOL("read-file", std_read_file); \
  ADD_FN_SYMBOL("write-file", std_write_file);
