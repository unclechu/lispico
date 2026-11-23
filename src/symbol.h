#pragma once

#include <stdbool.h>
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

// A byte string with optional read-only view metadata.
//
// `data` points to the first visible byte, while `base` identifies the
// beginning of its backing storage. Ordinary strings have `base == data`;
// future slices may point inside their original allocation.
//
// `immutable_ref` restricts writes through this view, not through other
// references to the same backing storage. No read-only views are created yet.
//
// `nul_terminated` indicates that `data[length]` is known to contain '\0'.
// Strings currently retain this guarantee whenever `data` is non-NULL;
// future read-only slices may not. Embedded NUL bytes remain valid.
// The termination flag is meaningful only when `data` is non-NULL.
// A zero-initialized `String` represents an empty string with no buffer.
typedef struct {
  const char *data;

  // A reference to the original string in case `data` is an immutable-ref
  // slice view of a string.
  const char *base;

  size_t length;

  bool immutable_ref;
  bool nul_terminated;
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
  void *data; // Raw C elements; string arrays contain `String` descriptors.
  void *base; // Backing allocation start; currently equal to `data`.
  bool immutable_ref; // Future read-only views can share backing storage.
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

typedef struct Symbol {
  char name[SYMBOL_NAME_SIZE];
  SymbolKind kind;

  // Once frozen, neither the symbol nor the storage it owns may be modified
  // during this evaluation. A new evaluation starts with a clean symbol table.
  bool frozen;

  // Local symbols remain identifiable after their scope has been cleaned up.
  // Their names may still occupy entries in the global symbol table.
  bool defined_as_local;

  // Active `(do)` activation that owns this local symbol, if any.
  // These links do not affect the global symbol table or its probing.
  //
  // After leaving `(do)` scope the local variable is fully reset to
  // `TYPE_VOID`. And these pointers are also reset to `NULL`. So after cleanup
  // happens the `(do)` scope does not own them anymore, but the `TYPE_VOID`
  // symbols stay in the symbols table (there are no deletions). Next `(do)`
  // evaluation would just re-use/override them by another `(local)`/`(local-*)`
  // call.
  struct EvalLocalScopeFrame *local_scope_owner;
  struct Symbol *next_local;

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
Symbol* symbol_lookup_scoped(Interpreter *interpreter, const char *name);
Symbol* symbol_lookup_introspection(Interpreter *interpreter, const char *name);
bool local_symbol_name(
  uint32_t expression_index,
  const char *name,
  char (*result)[SYMBOL_NAME_SIZE]
);
void register_local_symbol(Interpreter *interpreter, Symbol *symbol);
Symbol* symbol_define(Interpreter *interpreter, const char *name, SymbolKind kind);
void clean_symbols(Interpreter *interpreter);

// Error outcomes for the `symbol_define` function.
typedef enum {
  INCORRECT_INPUT = 0,
  ALREADY_DEFINED,
  TABLE_IS_FULL,
} symbol_define_error_t;
