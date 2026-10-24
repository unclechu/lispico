#pragma once

#include <stdint.h>
#include <stdlib.h>

#include "constants.h"

// A discriminator for the `Symbol` type
typedef enum {
  SYM_EMPTY = 0,
  SYM_VARIABLE,
  SYM_ARRAY,
  SYM_LABEL,
  SYM_NATIVE_FUNCTION,
} SymbolKind;

// Allowed data types for a value
typedef enum {
  TYPE_VOID = 0,
  TYPE_U32,
  TYPE_I32,
  TYPE_F32,
  TYPE_STRING,
} ValueType;

// Note that `\0` is not expected in the `data` string.
// These strings are not terminated, use `length`.
typedef struct {
  const char *data;
  size_t length;
} String;

// A value of a specific type
typedef struct {
  ValueType type;
  union {
    uint32_t u32; // `uint32`/`TYPE_U32` type
    int32_t i32; // `int32`/`TYPE_I32` type
    float f32; // `float`/`TYPE_F32` type
    String string; // String `TYPE_STRING` type
  } as;
} Value;

typedef struct {
  ValueType element_type;
  uint32_t length;
  void *data; // An array pointer with raw C types (except `String` is used for strings)
} Array;

// A native C-function pointer to be stored in a `Symbol`.
// Allowing you to make wrapper functions compatible with the interpreted
// language that would execute arbitrary C-code (e.g. FFI bindings for IO).
typedef Value (*NativeFunction)(const Value *args, size_t argc);

typedef struct {
  char name[SYMBOL_NAME_SIZE];
  SymbolKind kind;

  union {
    // `kind == SYM_VARIABLE`
    Value variable;

    // `kind == SYM_ARRAY`
    Array array;

    // `kind == SYM_LABEL`
    // Index of the instruction to execute when jumping to this label.
    uint32_t instruction_index;

    // `kind == SYM_NATIVE_FUNCTION`
    // A native C function stored. So that arbitrary C-functions can extend
    // the `symbols` table.
    NativeFunction native_function;
  } as;
} Symbol;

Symbol* symbol_lookup(const char *name);
Symbol* symbol_define(const char *name, SymbolKind kind);
void clean_symbols();

// Error outcomes for the `symbol_define` function.
typedef enum {
  INCORRECT_INPUT = 0,
  ALREADY_DEFINED,
  TABLE_IS_FULL,
} symbol_define_error_t;

// Error from the most recent failed `symbol_define` call.
extern symbol_define_error_t symbol_define_error;
