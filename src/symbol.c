#include <string.h>

#include "symbol.h"
#include "wipe-memory.h"

// `Symbol`s hash-table (program state)
static Symbol symbols[SYMBOL_CAPACITY];

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
  // Getting hash-table index between 0 and `SYMBOL_CAPACITY`-1.
  // Modulo over `SYMBOL_CAPACITY`.
  size_t i = hash_name(name) & (SYMBOL_CAPACITY - 1u);

  // Finding the `Symbol` by name starting with calculated hash address.
  // Shifting forward in case of a hash-collision.
  for (size_t n = 0; n < SYMBOL_CAPACITY; ++n) {
    // Starting with calculating hash-address.
    Symbol *s = &symbols[i];

    // No Symbol present by this hash address (`Symbol` not found).
    // Works without extra trickery since there are no deletions.
    if (s->kind == SYM_EMPTY) return NULL;

    // Name is matching, `Symbol` is found.
    // Otherwise it’s a hash-collision, continue search.
    if (strcmp(s->name, name) == 0) return s;

    // Shift index of the hash table (with modulo over `SYMBOL_CAPACITY`).
    // Slot occupied by another name; continue probing.
    i = (i + 1u) & (SYMBOL_CAPACITY - 1u);
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
  size_t len = strlen(name);

  // If `Symbol` name is empty, or exceeds `Symbol` name size limit,
  // or provided `kind` is `SYM_EMPTY` then abort operation as an error.
  if (len == 0 || len >= SYMBOL_NAME_SIZE || kind == SYM_EMPTY) {
    symbol_define_error = INCORRECT_INPUT;
    return NULL;
  }

  // Bit-wise modulo over the hash of the name
  // (initiall guess, can be shifted forward in case of a hash conflict).
  size_t i = hash_name(name) & (SYMBOL_CAPACITY - 1u);

  // Resolve potential hash-conflicts.
  for (size_t n = 0; n < SYMBOL_CAPACITY; ++n) {
    // Starting with calculated hash-address.
    Symbol *s = &symbols[i];

    // Free spot found, continuing with defining a symbol.
    if (s->kind == SYM_EMPTY) {
      memcpy(s->name, name, len + 1u); // Plus 1u for \0 termination
      s->kind = kind;
      return s;
    }

    // `Symbol` with specified name is already defined (error)
    if (strcmp(s->name, name) == 0) {
      symbol_define_error = ALREADY_DEFINED;
      return NULL;
    }

    // Shift index of the hash table (with modulo over `SYMBOL_CAPACITY`).
    // Slot occupied by another name; continue probing.
    i = (i + 1u) & (SYMBOL_CAPACITY - 1u);
  }

  symbol_define_error = TABLE_IS_FULL;
  return NULL; // `symbols` hash-table is full (error)
}

// Reset `symbols` state (flush the hash-table)
void clean_symbols() {
  wipe_memory(symbols, sizeof(symbols));
  // TODO: wipe the allocated strings and arrays (runtime allocation)
}
