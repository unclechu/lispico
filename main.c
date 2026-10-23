#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// *** Constants ***

// Must be a power of two (for bitwise-modulo optimizations to work — &)
#define SYMBOL_CAPACITY 256u
// Maximum Symbol name length (including \0 terminator).
#define SYMBOL_NAME_SIZE 128u

// Maximum error message size
#define ERR_MSG_SIZE 2048u

// *** `symbols` (definitions) hash-table ***

// A discriminator for the `Symbol` type
typedef enum {
  SYM_EMPTY = 0,
  SYM_VARIABLE,
  SYM_ARRAY,
  SYM_LABEL,
  SYM_NATIVE_FUNCTION,
} SymbolKind;

// Get a string representation of `SymbolKind` value
const char* show_SymbolKind(SymbolKind kind) {
  switch (kind) {
    case SYM_EMPTY: return "SYM_EMPTY";
    case SYM_VARIABLE: return "SYM_VARIABLE";
    case SYM_ARRAY: return "SYM_ARRAY";
    case SYM_LABEL: return "SYM_LABEL";
    case SYM_NATIVE_FUNCTION: return "SYM_NATIVE_FUNCTION";
    default: return "( ! Unexpected SymbolKind for show_SymbolKind ! )";
  }
}

// Allowed data types for a value
typedef enum {
  TYPE_VOID = 0,
  TYPE_U32,
  TYPE_I32,
  TYPE_F32,
  TYPE_STRING,
} ValueType;

// Get a string representation of `ValueType` value
const char* show_ValueType(ValueType type) {
  switch (type) {
    case TYPE_VOID: return "TYPE_VOID";
    case TYPE_U32: return "TYPE_U32";
    case TYPE_I32: return "TYPE_I32";
    case TYPE_F32: return "TYPE_F32";
    case TYPE_STRING: return "TYPE_STRING";
    default: return "( ! Unexpected ValueType for show_ValueType ! )";
  }
}

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

// A native C-function pointer to be stored in a `Symbol`.
// Allowing you to make wrapper functions compatible with the interpreted
// language that would execute arbitrary C-code (e.g. FFI bindings for IO).
typedef Value (*NativeFunction)(const Value *args, size_t argc);

typedef struct {
  ValueType element_type;
  uint32_t length;
  void *data;
} Array;

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
static Symbol* symbol_lookup(const char *name) {
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

// Error outcomes for the `symbol_define` function.
typedef enum {
  INCORRECT_INPUT = 0,
  ALREADY_DEFINED,
  TABLE_IS_FULL,
} symbol_define_error_t;

static symbol_define_error_t symbol_define_error;

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
static Symbol* symbol_define(const char *name, SymbolKind kind) {
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

void extract_error_message_from_symbol_define_error(
  char (*error_message)[ERR_MSG_SIZE],
  const char *symbol_name
) {
  snprintf(*error_message, sizeof(*error_message), "symbol_define for “%s”: ", symbol_name);
  switch (symbol_define_error) {
    case INCORRECT_INPUT:
      snprintf(
        *error_message + strlen(*error_message),
        sizeof(*error_message) - strlen(*error_message),
        "Received incorrect input"
      );
      break;
    case ALREADY_DEFINED:
      snprintf(
        *error_message + strlen(*error_message),
        sizeof(*error_message) - strlen(*error_message),
        "Name is already defined"
      );
      break;
    case TABLE_IS_FULL:
      snprintf(
        *error_message + strlen(*error_message),
        sizeof(*error_message) - strlen(*error_message),
        "Failed because symbols hash-table is full"
      );
      break;
    default:
      snprintf(
        *error_message + strlen(*error_message),
        sizeof(*error_message) - strlen(*error_message),
        "Undefined error type (should never happen)"
      );
  }
}

// *** Evaluator ***

typedef enum {
  EVAL_OK = 0,
  EVAL_EXIT,
  EVAL_FAIL,
} EvalOutcomeKind;

typedef struct {
  EvalOutcomeKind kind;
  Value value; // Resulting value of expression evaluation
  union {
    // `kind == EVAL_EXIT`
    int exit_code;
    // `kind == EVAL_FAIL`
    char fail_msg[ERR_MSG_SIZE];
  } as;
} EvalOutcome;

static bool populate_std(char (*error_message)[ERR_MSG_SIZE]);

// Current expression evaluation result.
//
// Evaluator must reset this value at the beginning of the program evaluation
// (to prevent stale evaluation state).
//
// When `kind` is either `EVAL_EXIT` or `EVAL_FAIL` the evaluator must stop
// evaluating the program.
//
// When a `NativeFunction` fails it must set `kind` to `EVAL_FAIL` and provide
// `fail_msg`.
static EvalOutcome eval_outcome = {
  .kind = EVAL_OK,
  .value = { .type = TYPE_VOID },
};

EvalOutcome evaluate() {
  // Reset evaluation state
  eval_outcome = (EvalOutcome) {
    .kind = EVAL_OK,
    .value = { .type = TYPE_VOID },
  };

  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  char fail_prefix[64] = {'\0'};

  snprintf(fail_prefix, sizeof(fail_prefix), "populate_std failed: ");
  if (!populate_std(&fail_msg)) goto fail;

  return eval_outcome;

  fail:
    EvalOutcome result = {
      .kind = EVAL_FAIL,
      .value = { .type = TYPE_VOID },
      .as.fail_msg = {'\0'},
    };
    snprintf(
      result.as.fail_msg,
      sizeof(result.as.fail_msg),
      "%s%s",
      fail_prefix,
      fail_msg
    );
    return result;
}

// *** Standard library ***

// Branching between different kinds of standard library printing functions
typedef enum {
  STD_PRINT = 0,
  STD_PRINTLN,
  STD_EPRINT,
  STD_EPRINTLN,
} StdPrintVariant;

// Get a string representation of `show_StdPrintVariant` value
const char* show_StdPrintVariant(StdPrintVariant variant) {
  switch (variant) {
    case STD_PRINT: return "(print)";
    case STD_PRINTLN: return "(println)";
    case STD_EPRINT: return "(eprint)";
    case STD_EPRINTLN: return "(eprintln)";
    default: return "( ! Unexpected StdPrintVariant for show_StdPrintVariant ! )";
  }
}

// Generic standard library printing implementation
Value std_generic_print(StdPrintVariant variant, const Value *args, size_t argc) {
  Value return_value = { .type = TYPE_VOID };

  if (argc != 1) {
    eval_outcome.kind = EVAL_FAIL;
    snprintf(
      eval_outcome.as.fail_msg,
      sizeof(eval_outcome.as.fail_msg),
      "%s expects exactly 1 argument, got instead: %d",
      show_StdPrintVariant(variant),
      (int)argc
    );
    return return_value;
  } else if (args[0].type != TYPE_STRING) {
    eval_outcome.kind = EVAL_FAIL;
    snprintf(
      eval_outcome.as.fail_msg,
      sizeof(eval_outcome.as.fail_msg),
      "%s argument must be a string, got instead: %s",
      show_StdPrintVariant(variant),
      show_ValueType(args[0].type)
    );
    return return_value;
  }

  FILE *stream =
    (variant == STD_EPRINT || variant == STD_EPRINTLN)
      ? stderr
      : stdout;

  fwrite(
    args[0].as.string.data,
    sizeof(char),
    args[0].as.string.length,
    stream
  );

  if (variant == STD_PRINTLN || variant == STD_EPRINTLN) fputc('\n', stream);

  return return_value;
}

Value std_print(const Value *args, size_t argc) {
  return std_generic_print(STD_PRINT, args, argc);
}

Value std_println(const Value *args, size_t argc) {
  return std_generic_print(STD_PRINTLN, args, argc);
}

Value std_eprint(const Value *args, size_t argc) {
  return std_generic_print(STD_EPRINT, args, argc);
}

Value std_eprintln(const Value *args, size_t argc) {
  return std_generic_print(STD_EPRINTLN, args, argc);
}

// Populate the `symbols` with “standard library” definitions
static bool populate_std(char (*error_message)[ERR_MSG_SIZE]) {
  const char *symbol_name;

  symbol_name = "print";
  Symbol *std_print_sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
  if (std_print_sym == NULL) goto symbol_define_fail;
  std_print_sym->as.native_function = std_print;

  symbol_name = "println";
  Symbol *std_println_sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
  if (std_println_sym == NULL) goto symbol_define_fail;
  std_println_sym->as.native_function = std_println;

  symbol_name = "eprint";
  Symbol *std_eprint_sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
  if (std_eprint_sym == NULL) goto symbol_define_fail;
  std_eprint_sym->as.native_function = std_eprint;

  symbol_name = "eprintln";
  Symbol *std_eprintln_sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
  if (std_eprintln_sym == NULL) goto symbol_define_fail;
  std_eprintln_sym->as.native_function = std_eprintln;

  return true;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(error_message, symbol_name);
    return false;
}

// *** Main program ***

int main() {
  EvalOutcome eval_outcome = evaluate();

  switch (eval_outcome.kind) {
    case EVAL_OK:
    case EVAL_EXIT:
      const char* value_type = show_ValueType(eval_outcome.value.type);
      switch (eval_outcome.value.type) {
        case TYPE_VOID:
          printf("Value %s: void\n", value_type);
          break;
        case TYPE_U32:
          printf("Value %s: %d\n", value_type, eval_outcome.value.as.u32);
          break;
        case TYPE_I32:
          printf("Value %s: %d\n", value_type, eval_outcome.value.as.i32);
          break;
        case TYPE_F32:
          printf("Value %s: %.06f\n", value_type, eval_outcome.value.as.f32);
          break;
        case TYPE_STRING:
          printf("Value %s: %s\n", value_type, eval_outcome.value.as.string.data);
          break;
        default:
          fprintf(stderr, "[FAIL] Unexpected result type: %d\n", eval_outcome.value.type);
          return 1;
      }
      if (eval_outcome.kind == EVAL_EXIT) {
        printf("[OK] Evaluation finished with exit code: %d\n", eval_outcome.as.exit_code);
        return eval_outcome.as.exit_code;
      } else {
        printf("[OK] Evaluation was okay\n");
        return 0;
      }
      break;
    default:
      fprintf(stderr, "[FAIL] Unexpected EvalOutcome kind: %d\n", eval_outcome.kind);
      return 1;
  }
}
