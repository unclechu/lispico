#pragma once

// Supress the warning: ISO C forbids an empty translation unit [-Wpedantic]
// This module is intended to contain only macroses.
typedef int stdlib_macros_not_empty;

#define ADD_STDLIB_FN_TYPE_DEF(name) Datum name(const Datum*, size_t argc)

#define ADD_FN_SYMBOL(name, fn) { \
  symbol_name = name; \
  Symbol *sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION); \
  if (sym == NULL) goto symbol_define_fail; \
  sym->as.native_function = fn; \
}

#define ADD_VAR_SYMBOL(name, type_id, as_type, val) { \
  symbol_name = name; \
  Symbol *sym = symbol_define(symbol_name, SYM_VARIABLE); \
  if (sym == NULL) goto symbol_define_fail; \
  sym->as.variable = (Value) { \
    .type = type_id, \
    .as.as_type = val, \
  }; \
}
