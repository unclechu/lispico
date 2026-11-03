#pragma once

// Supress the warning: ISO C forbids an empty translation unit [-Wpedantic]
// This module is intended to contain only macros.
typedef int stdlib_macros_not_empty;

// Context dependencies:
// - `Interpreter` type
// - `Datum` type
// - `size_t` type
#define ADD_STDLIB_FN_TYPE_DEF(name) \
  Datum name(Interpreter *interpreter, const Datum*, size_t argc)

// Context dependencies:
// - `Interpreter *interpreter` variable with properly initialized value
// - `const char *symbol_name` variable (doesn’t have to be initialized)
// - `symbol_define_fail` error handling label
// - `SYM_NATIVE_FUNCTION` from `symbol.h`
// - `Symbol` type from `symbol.h`
// - `symbol_define` function from `symbol.h`
#define ADD_FN_SYMBOL(name, fn) { \
  symbol_name = name; \
  Symbol *sym = symbol_define(interpreter, symbol_name, SYM_NATIVE_FUNCTION); \
  if (sym == NULL) goto symbol_define_fail; \
  sym->as.native_function = fn; \
}

// Context dependencies:
// - `Interpreter *interpreter` variable with properly initialized value
// - `const char *symbol_name` variable (doesn’t have to be initialized)
// - `symbol_define_fail` error handling label
// - `SYM_VARIABLE` from `symbol.h`
// - `Symbol` type from `symbol.h`
// - `symbol_define` function from `symbol.h`
#define ADD_VAR_SYMBOL(name, type_id, as_type, val) { \
  symbol_name = name; \
  Symbol *sym = symbol_define(interpreter, symbol_name, SYM_VARIABLE); \
  if (sym == NULL) goto symbol_define_fail; \
  sym->as.variable = (Value) { \
    .type = type_id, \
    .as.as_type = val, \
  }; \
}
