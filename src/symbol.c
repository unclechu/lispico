#include <string.h>

#include "symbol.h"
#include "wipe-memory.h"
#include "storage.h"

// Calculate hash for a given string (simple and fast FNV-1a algorithm)
static uint32_t hash_name(const char *name) {
  uint32_t hash = 2166136261u;
  // Keep XOR-ing and multiplying until `*name` is `\0` (terminated)
  for (; *name; ++name) {
    hash ^= (unsigned char)*name;
    hash *= 16777619u;
  }
  return hash;
}

/*
  Find a `Symbol` in a `symbols` hash-table.

  Returns `NULL` if `Symbol` is not found.

  Usage example:

  ```c
  Symbol *s = symbol_lookup("foo");

  if (s && s->kind == SYM_VARIABLE) {
    uint32_t value = s->as.variable.as.u32;
  }
  ```
*/
Symbol* symbol_lookup(const char *name) {
  // Getting hash-table index between 0 and `symbol_capacity`-1.
  // Modulo over `symbol_capacity`.
  size_t i = hash_name(name) & (symbol_capacity - 1u);

  // Finding the `Symbol` by name starting with calculated hash address.
  // Shifting forward in case of a hash-collision.
  for (size_t n = 0; n < symbol_capacity; ++n) {
    // Starting with calculating hash-address.
    Symbol *s = &symbols[i];

    // No Symbol present by this hash address (`Symbol` not found).
    // Works without extra trickery since there are no deletions.
    if (s->kind == SYM_EMPTY) return NULL;

    // Name is matching, `Symbol` is found.
    // Otherwise it’s a hash-collision, continue search.
    if (strcmp(s->name, name) == 0) return s;

    // Shift index of the hash table (with modulo over `symbol_capacity`).
    // Slot occupied by another name; continue probing.
    i = (i + 1u) & (symbol_capacity - 1u);
  }

  return NULL; // `Symbol` not found
}

symbol_define_error_t symbol_define_error;

/*
  Define a new symbol in the `symbols` hash-table.

  Returns `NULL` and updates `symbol_define_error` in case of an error:

  - `symbol_define_error == INCORRECT_INPUT` —
    Either `Symbol` name is wrong (empty or exceeds limit)
    or `Symbol` kind is `SYM_EMPTY`
  - `symbol_define_error == ALREADY_DEFINED` —
    A `Symbol` with this name is already defined
  - `symbol_define_error == TABLE_IS_FULL` —
    The `symbols` hash-table is full (no space left)

  Usage example:

  ```
  Symbol *s = symbol_define("foo", SYM_VARIABLE);

  if (s) {
    s->as.variable = (Value) {
      .type = TYPE_U32,
      .as.u32 = 123
    };
  }
  ```
*/
Symbol* symbol_define(const char *name, SymbolKind kind) {
  if (symbols == NULL || symbol_capacity == 0) {
    symbol_define_error = TABLE_IS_FULL;
    return NULL;
  }

  size_t len = strlen(name);

  // If `Symbol` name is empty, or exceeds `Symbol` name size limit,
  // or provided `kind` is `SYM_EMPTY` then abort operation as an error.
  if (len == 0 || len >= SYMBOL_NAME_SIZE || kind == SYM_EMPTY) {
    symbol_define_error = INCORRECT_INPUT;
    return NULL;
  }

  // Bit-wise modulo over the hash of the name
  // (initiall guess, can be shifted forward in case of a hash conflict).
  size_t i = hash_name(name) & (symbol_capacity - 1u);

  // Resolve potential hash-conflicts.
  for (size_t n = 0; n < symbol_capacity; ++n) {
    // Starting with calculated hash-address.
    Symbol *s = &symbols[i];

    // Free spot found, continuing with defining a symbol.
    if (s->kind == SYM_EMPTY) {
      memcpy(s->name, name, len + 1u);
      s->kind = kind;
      symbol_count++;
      return s;
    }

    // `Symbol` with specified name is already defined (error)
    if (strcmp(s->name, name) == 0) {
      symbol_define_error = ALREADY_DEFINED;
      return NULL;
    }

    // Shift index of the hash table (with modulo over `symbol_capacity`).
    // Slot occupied by another name; continue probing.
    i = (i + 1u) & (symbol_capacity - 1u);
  }

  symbol_define_error = TABLE_IS_FULL;
  return NULL; // `symbols` hash-table is full (error)
}

// Reset `symbols` state (flush the hash-table)
void clean_symbols() {
  symbol_count = 0;
  if (symbols != NULL) wipe_memory(symbols, (size_t)symbol_capacity * sizeof(*symbols));
}
