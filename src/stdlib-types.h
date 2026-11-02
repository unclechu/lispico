#pragma once

#include "stdlib-macros.h"
#include "symbol.h"

ADD_STDLIB_FN_TYPE_DEF(std_type);
ADD_STDLIB_FN_TYPE_DEF(std_type_of);
ADD_STDLIB_FN_TYPE_DEF(std_kind_of);

#define POPULATE_STDLIB_TYPES() \
  /* Type identifiers */ \
  ADD_VAR_SYMBOL("void", TYPE_U32, u32, TYPE_VOID); \
  ADD_VAR_SYMBOL("u32", TYPE_U32, u32, TYPE_U32); \
  ADD_VAR_SYMBOL("i32", TYPE_U32, u32, TYPE_I32); \
  ADD_VAR_SYMBOL("f32", TYPE_U32, u32, TYPE_F32); \
  ADD_VAR_SYMBOL("s", TYPE_U32, u32, TYPE_STRING); \
  \
  /* Numeric type boundaries */ \
  ADD_VAR_SYMBOL("u32-min", TYPE_U32, u32, 0u); \
  ADD_VAR_SYMBOL("u32-max", TYPE_U32, u32, UINT32_MAX); \
  ADD_VAR_SYMBOL("i32-min", TYPE_I32, i32, INT32_MIN); \
  ADD_VAR_SYMBOL("i32-max", TYPE_I32, i32, INT32_MAX); \
  ADD_VAR_SYMBOL("f32-min", TYPE_F32, f32, -FLT_MAX); \
  ADD_VAR_SYMBOL("f32-max", TYPE_F32, f32, FLT_MAX); \
  /* Additional floating-point constants */ \
  ADD_VAR_SYMBOL("f32-min-positive", TYPE_F32, f32, FLT_MIN); \
  ADD_VAR_SYMBOL("f32-true-min", TYPE_F32, f32, FLT_TRUE_MIN); \
  ADD_VAR_SYMBOL("f32-epsilon", TYPE_F32, f32, FLT_EPSILON); \
  \
  /* Kind identifiers */ \
  ADD_VAR_SYMBOL("empty-kind", TYPE_U32, u32, SYM_EMPTY); \
  ADD_VAR_SYMBOL("var-kind", TYPE_U32, u32, SYM_VARIABLE); \
  ADD_VAR_SYMBOL("arr-kind", TYPE_U32, u32, SYM_ARRAY); \
  ADD_VAR_SYMBOL("label-kind", TYPE_U32, u32, SYM_LABEL); \
  ADD_VAR_SYMBOL("native-fn-kind", TYPE_U32, u32, SYM_NATIVE_FUNCTION); \
  \
  /* Type conversion */ \
  ADD_FN_SYMBOL("type", std_type); \
  \
  /* Type inspection */ \
  ADD_FN_SYMBOL("type-of", std_type_of); \
  ADD_FN_SYMBOL("kind-of", std_kind_of); \
  \
  /* Boolean constants (booleans are represented by u32 type) */ \
  ADD_VAR_SYMBOL("true", TYPE_U32, u32, 1u); \
  ADD_VAR_SYMBOL("false", TYPE_U32, u32, 0u);
