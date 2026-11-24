#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_immutable_ref);
ADD_STDLIB_FN_TYPE_DEF(std_is_immutable_ref);
ADD_STDLIB_FN_TYPE_DEF(std_has_immutable_refs);

#define POPULATE_STDLIB_IMMUTABLE_REFS() \
  ADD_FN_SYMBOL("immutable-ref", std_immutable_ref); \
  ADD_FN_SYMBOL("is-immutable-ref", std_is_immutable_ref); \
  ADD_FN_SYMBOL("has-immutable-refs", std_has_immutable_refs);
