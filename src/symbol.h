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

// The `data` (unless it is `NULL` when `length` is `0`) is supposed to be
// NUL-terminated. `length` does not include the termination symbol.
// `data` can contain NUL `\0` bytes inside as part of the string by design.
// But there is also `\0` at `[length]` index. For compatibility with C
// functions.
typedef struct {
  const char *data;
  size_t length;
} String;

// A value of a specific type
typedef struct {
  ValueType type;
  union {
    uint32_t u32; // `TYPE_U32` type
    int32_t i32; // `TYPE_I32` type
    float f32; // `TYPE_F32` type
    String string; // String `TYPE_STRING` type
  } as;
} Value;

typedef struct {
  ValueType element_type;
  uint32_t length;
  void *data; // An array pointer with raw C types (except `String` is used for strings)
} Array;

// A higher-level value (combining `Value` and `Array`).
//
// Note that `Array` can’t be part of `Value` because an `Array` contains
// `Value`s but there is no support for arrays for more than one dimension. So
// `Value` can’t contain `Array`s.
typedef struct {
  SymbolKind kind; // Only one of these: SYM_EMPTY, SYM_VARIABLE, SYM_ARRAY, SYM_LABEL
  union {
    Value value; // `kind == SYM_VARIABLE`
    Array array; // `kind == SYM_ARRAY`
    uint32_t instruction_index; // `kind == SYM_LABEL`
  } as;
} Datum;

// Forward declaration to avoid circular header dependencies.
typedef struct Interpreter Interpreter;

// A native C-function pointer to be stored in a `Symbol`.
// Allowing you to make wrapper functions compatible with the interpreted
// language that would execute arbitrary C-code (e.g. FFI bindings for IO).
typedef Datum (*NativeFunction)(Interpreter *interpreter, const Datum *args, size_t argc);

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

Symbol* symbol_lookup(Interpreter *interpreter, const char *name);
Symbol* symbol_define(Interpreter *interpreter, const char *name, SymbolKind kind);
void clean_symbols(Interpreter *interpreter);

// Error outcomes for the `symbol_define` function.
typedef enum {
  INCORRECT_INPUT = 0,
  ALREADY_DEFINED,
  TABLE_IS_FULL,
} symbol_define_error_t;
