#include <string.h>
#include <inttypes.h>

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
  Find a `Symbol` in a `interpreter->storage.symbols` hash-table.

  Returns `NULL` if `Symbol` is not found.

  Usage example:

  ```c
  Symbol *s = symbol_lookup(interpreter, "foo");

  if (s && s->kind == SYM_VARIABLE) {
    uint32_t value = s->as.variable.as.u32;
  }
  ```
*/
Symbol* symbol_lookup(Interpreter *interpreter, const char *name) {
  if (interpreter == NULL || !interpreter->configured || name == NULL) return NULL;

  Symbol *symbols = interpreter->storage.symbols;
  uint32_t symbol_capacity = interpreter->storage.symbol_capacity;

  if (symbols == NULL || symbol_capacity == 0) return NULL;

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

// Build the symbol name belonging to a particular `(do)` expression.
//
// Names that do not fit in the symbol table cannot be defined as locals.
//
// This function should be the single “source of truth” for the local-scoped
// variable name template.
bool local_symbol_name(
  uint32_t expression_index,
  const char *name,
  char (*result)[SYMBOL_NAME_SIZE]
) {
  if (name == NULL) return false;
  int length = snprintf(
    *result,
    sizeof(*result),
    "_LOCAL_%" PRIu32 "_%s",
    expression_index,
    name
  );
  return length > 0 && (size_t)length < sizeof(*result);
}

// Resolve a name from the innermost active `(do)` to the global namespace.
Symbol* symbol_lookup_scoped(Interpreter *interpreter, const char *name) {
  if (interpreter == NULL || name == NULL) return NULL;

  for (
    const EvalLocalScopeFrame *scope = interpreter->local_scope;
    scope != NULL;
    scope = scope->previous
  ) {
    char local_name[SYMBOL_NAME_SIZE] = {'\0'};
    if (!local_symbol_name(scope->expression_index, name, &local_name)) {
      wipe_memory(local_name, sizeof(local_name));
      continue;
    }

    // Regular symbol lookup using prefixed name.
    Symbol *symbol = symbol_lookup(interpreter, local_name);
    wipe_memory(local_name, sizeof(local_name));
    if (symbol != NULL) return symbol;
  }

  return symbol_lookup(interpreter, name);
}

// Register a local for cleanup when its owning `(do)` terminates.
void register_local_symbol(Interpreter *interpreter, Symbol *symbol) {
  EvalLocalScopeFrame *scope = interpreter->local_scope;
  if (scope == NULL || symbol->local_scope_owner == scope) return;

  // Put the `Symbol` on top of the local symbols LIFO stack.
  symbol->defined_as_local = true;
  symbol->next_local = scope->first_local;
  symbol->local_scope_owner = scope;
  scope->first_local = symbol;
}

/*
  Define a new symbol in the `interpreter->storage.symbols` hash-table.

  Returns `NULL` and updates `interpreter->symbol_define_error` in case of an error:

  - `interpreter->symbol_define_error == INCORRECT_INPUT` —
    Either `Symbol` name is wrong (empty or exceeds limit)
    or reserved (`empty`), or `Symbol` kind is `SYM_EMPTY`
  - `interpreter->symbol_define_error == ALREADY_DEFINED` —
    A `Symbol` with this name is already defined
  - `interpreter->symbol_define_error == TABLE_IS_FULL` —
    The `symbols` hash-table is full (no space left)

  Usage example:

  ```c
  Symbol *s = symbol_define(interpreter, "foo", SYM_VARIABLE);

  if (s) {
    s->as.variable = (Value) {
      .type = TYPE_U32,
      .as.u32 = 123
    };
  }
  ```
*/
Symbol* symbol_define(Interpreter *interpreter, const char *name, SymbolKind kind) {
  if (interpreter == NULL || !interpreter->configured) return NULL;

  Symbol *symbols = interpreter->storage.symbols;
  uint32_t symbol_capacity = interpreter->storage.symbol_capacity;

  if (symbols == NULL || symbol_capacity == 0) {
    interpreter->symbol_define_error = TABLE_IS_FULL;
    return NULL;
  }

  size_t len = strlen(name);

  // `empty` is a reserved literal, not a name that can be defined.
  // Otherwise, reject empty, oversized, and SYM_EMPTY symbol definitions.
  if (len == 0 || len >= SYMBOL_NAME_SIZE || kind == SYM_EMPTY || strcmp(name, "empty") == 0) {
    interpreter->symbol_define_error = INCORRECT_INPUT;
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
      s->frozen = false;
      s->defined_as_local = false;
      interpreter->symbol_count++;
      return s;
    }

    // `Symbol` with specified name is already defined (error)
    if (strcmp(s->name, name) == 0) {
      interpreter->symbol_define_error = ALREADY_DEFINED;
      return NULL;
    }

    // Shift index of the hash table (with modulo over `symbol_capacity`).
    // Slot occupied by another name; continue probing.
    i = (i + 1u) & (symbol_capacity - 1u);
  }

  interpreter->symbol_define_error = TABLE_IS_FULL;
  return NULL; // `symbols` hash-table is full (error)
}

// Reset `symbols` state (flush the hash-table)
void clean_symbols(Interpreter *interpreter) {
  if (interpreter == NULL || !interpreter->configured) return;

  interpreter->symbol_count = 0;

  if (interpreter->storage.symbols != NULL)
    wipe_memory(
      interpreter->storage.symbols,
      (size_t)interpreter->storage.symbol_capacity * sizeof(*interpreter->storage.symbols)
    );
}
