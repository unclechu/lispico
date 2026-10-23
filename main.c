#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>

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

// Note that `\0` is not expected in the `data` string.
// These strings are not terminated, use `length`.
typedef struct {
  const char *data;
  size_t length;
} String;

// `show_String` into a specified pointer.
//
// Requires a buffer of at least (string.length + 3) chars.
// Writes '\0' terminator at the end and wraps the string into double quotes.
static void show_String_to_pointer(char *dest, String string) {
  dest[0] = '"';
  // +1 to shift quotation mark
  if (string.length > 0) memcpy(dest + 1, string.data, sizeof(char) * string.length);
  dest[string.length + 1] = '"';
  dest[string.length + 2] = '\0';
}

// Make a printable string representation of a `String`.
//
// Will wrap the string into quotation marks.
// Note that `\0` is not expected in the `String` value.
// Will return `NULL` in case of failure (e.g. `malloc`).
//
// WARNING! Caller must free the allocated resulting string!
char* show_String(String string) {
  // Plus two quotation marks and one '\0' terminator.
  size_t shown_string_length = string.length + 3;
  size_t shown_string_size = sizeof(char) * shown_string_length;
  char *shown_string = malloc(shown_string_size);
  if (shown_string == NULL) return NULL; // Likely OOM
  show_String_to_pointer(shown_string, string);
  return shown_string;
}

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

// Writes a `Value` into the provided buffer.
//
// When `dest == NULL` and `capacity == 0`, only calculates
// the required number of characters (excluding '\0').
//
// Returns the number of characters, or -1 on failure.
static int show_Value_to_pointer(char *dest, size_t capacity, Value value, bool include_type) {
  const char *shown_type = show_ValueType(value.type);
  switch (value.type) {
    case TYPE_VOID: return include_type
      ? snprintf(dest, capacity, "%s(void)", shown_type)
      : snprintf(dest, capacity, "void");
    case TYPE_U32: return include_type
      ? snprintf(dest, capacity, "%s(%" PRIu32 ")", shown_type, value.as.u32)
      : snprintf(dest, capacity, "%" PRIu32, value.as.u32);
    case TYPE_I32: return include_type
      ? snprintf(dest, capacity, "%s(%" PRId32 ")", shown_type, value.as.i32)
      : snprintf(dest, capacity, "%" PRId32, value.as.i32);
    case TYPE_F32: return include_type
      ? snprintf(dest, capacity, "%s(%.6f)", shown_type, value.as.f32)
      : snprintf(dest, capacity, "%.6f", value.as.f32);
    case TYPE_STRING: {
      String string = value.as.string;
      size_t type_length = include_type ? strlen(shown_type) : 0;

      // +2 for quotation marks, +2 for parentheses if `include_type`
      size_t length = type_length + string.length + 2 + (include_type ? 2 : 0);

      if (dest != NULL) {
        if (capacity <= length) return -1;
        size_t pos = 0;
        if (include_type) {
          memcpy(dest, shown_type, sizeof(char) * type_length);
          pos += type_length;
          dest[pos++] = '(';
        }
        show_String_to_pointer(dest + pos, string);
        pos += string.length + 2; // Account for quotation marks from `show_String_to_pointer`
        if (include_type) dest[pos++] = ')'; // Overwrites `\0` from `show_String_to_pointer`
        dest[pos] = '\0';
      }

      return (int)length;
    }
    default: return snprintf(dest, capacity, "( ! Unexpected ValueType for show_Value ! )");
  }
}

// Make a printable string representation of a `Value`.
//
// Will return `NULL` in case of failure (e.g. `malloc`).
//
// WARNING! Caller must free the allocated resulting string!
char* show_Value(Value value) {
  int length = show_Value_to_pointer(NULL, 0, value, true);
  if (length < 0) return NULL;
  size_t size = sizeof(char) * ((size_t)length + 1); // +1 for `\0`.
  char *shown = malloc(size);
  if (shown == NULL) return NULL; // Likely OOM
  if (show_Value_to_pointer(shown, size, value, true) < 0) {
    free(shown);
    return NULL;
  }
  return shown;
}

typedef struct {
  ValueType element_type;
  uint32_t length;
  void *data; // An array pointer with raw C types (except `String` is used for strings)
} Array;

// Write `Array` element (taken by its index) into `Value` pointer.
static bool write_Array_item_to_Value(const Array *array, uint32_t index, Value *value) {
  value->type = array->element_type;
  switch (array->element_type) {
    case TYPE_VOID: return true; // Void does not have associated value
    case TYPE_U32:
      value->as.u32 = ((const uint32_t*)array->data)[index];
      return true;
    case TYPE_I32:
      value->as.i32 = ((const int32_t*)array->data)[index];
      return true;
    case TYPE_F32:
      value->as.f32 = ((const float*)array->data)[index];
      return true;
    case TYPE_STRING:
      value->as.string = ((const String*)array->data)[index];
      return true;
    default: return false; // Unexpected type ()
  }
}

// Make a printable string representation of an `Array` value.
//
// Note that `\0` is not expected in the `String` values.
// Will return `NULL` in case of failure (e.g. `malloc` or `snprintf` error).
//
// WARNING! Caller must free the allocated resulting string!
char* show_Array(Array array) {
  const char *shown_type = show_ValueType(array.element_type);

  // Calculate prefix length.
  const int prefix_length = snprintf(NULL, 0, "ARRAY(%s, [", shown_type);
  if (prefix_length < 0) return NULL;

  // Include two closing characters: "])".
  size_t length = (size_t)prefix_length + 2;

  // Calculate memory required for all elements.
  for (uint32_t i = 0; i < array.length; ++i) {
    Value element;
    if (!write_Array_item_to_Value(&array, i, &element)) return NULL;
    int part_length = show_Value_to_pointer(NULL, 0, element, false);
    if (part_length < 0) return NULL;
    length += (size_t)part_length;
    // Comma and space between elements.
    if (i != 0) length += 2;
  }

  // Allocate resulting string, +1 for '\0'.
  const size_t size = sizeof(char) * (length + 1);
  char *shown = malloc(size);
  if (shown == NULL) return NULL; // Likely OOM

  // Write prefix.
  int written = snprintf(shown, size, "ARRAY(%s, [", shown_type);

  if (written < 0 || (size_t)written >= size) {
    free(shown);
    return NULL;
  }

  size_t pos = (size_t)written;

  // Write array elements directly into allocated memory.
  for (uint32_t i = 0; i < array.length; ++i) {
    Value element;
    if (!write_Array_item_to_Value(&array, i, &element)) {
      free(shown);
      return NULL;
    }

    // Add separator
    if (i != 0) {
      const char separator[] = ", ";
      const size_t separator_length = strlen(separator);
      // Not include `\0` terminator (automatically added to the string literal).
      // So not taking `sizeof(separator)` because of this.
      memcpy(shown + pos, separator, sizeof(char) * separator_length);
      pos += separator_length;
    }

    int part_length = show_Value_to_pointer(shown + pos, size - pos, element, false);
    if (part_length < 0) {
      free(shown);
      return NULL;
    }

    pos += (size_t)part_length;
  }

  // Write closing brackets and '\0' (string literals are terminated).
  const char suffix[] = "])";
  memcpy(shown + pos, suffix, sizeof(suffix));

  return shown;
}

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
  SymbolKind kind; // Only one of these: SYM_EMPTY, SYM_VARIABLE, SYM_ARRAY
  union {
    Value value; // `kind == SYM_VARIABLE`
    Array array; // `kind == SYM_ARRAY`
  } as;
} EvalResult;

typedef struct {
  EvalOutcomeKind kind;
  EvalResult result; // Result of expression evaluation
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
  .result = { .kind = SYM_EMPTY },
};

typedef struct EvalSettingsInterface {
  void *context; // Internal context
  void *state;

  // Caller is expect to stop settings callback evaluation if `NULL` is returned
  // (it indicates a failure, which is automatically forwarded to `EvalOutcome`).
  Symbol* (*define_symbol)(
    const struct EvalSettingsInterface *self,
    const char *name,
    SymbolKind kind
  );

  Symbol* (*symbol_lookup)(
    const struct EvalSettingsInterface *self,
    const char *name
  );

  void (*report_failure)(
    const struct EvalSettingsInterface *self,
    const char *err_msg
  );
} EvalSettingsInterface;

typedef struct {
  bool is_failed;
  char fail_msg[ERR_MSG_SIZE];
} EvalSettingsInterfaceContext;

static Symbol* define_symbol_interface(
  const struct EvalSettingsInterface *self,
  const char *name,
  SymbolKind kind
) {
  EvalSettingsInterfaceContext *ctx = (EvalSettingsInterfaceContext*) self->context;
  if (ctx->is_failed) return NULL; // Already failed, can’t continue
  Symbol *sym = symbol_define(name, kind);
  if (sym == NULL) {
    extract_error_message_from_symbol_define_error(&ctx->fail_msg, name);
    return NULL;
  } else {
    return sym;
  }
}

static Symbol* symbol_lookup_interface(
  const struct EvalSettingsInterface*,
  const char *name
) {
  return symbol_lookup(name);
}

static void report_failure_interface(
  const struct EvalSettingsInterface *self,
  const char *err_msg
) {
  EvalSettingsInterfaceContext *ctx = (EvalSettingsInterfaceContext*) self->context;
  ctx->is_failed = true;
  char err_msg_prefix[] = "settings callback failure report: ";
  snprintf(ctx->fail_msg, sizeof(ctx->fail_msg), "%s%s", err_msg_prefix, err_msg);
}

/*
  Evaluate an expression.

  Set `settings_cb` and `state` to `NULL` if you don’t need to modify any settings.
  `state` is forwarded to `settings_cb` via `EvalSettingsInterface`
  (see `EvalSettingsInterface::state`).
*/
EvalOutcome evaluate(
  void (*settings_cb)(EvalSettingsInterface*),
  void *state
) {
  // Reset evaluation state
  eval_outcome = (EvalOutcome) {
    .kind = EVAL_OK,
    .result = { .kind = SYM_EMPTY },
  };

  EvalSettingsInterfaceContext settings_ctx = {
    .is_failed = false,
    .fail_msg = {'\0'},
  };

  char fail_prefix[64] = {'\0'};

  snprintf(settings_ctx.fail_msg, sizeof(settings_ctx.fail_msg), "populate_std failed: ");
  if (!populate_std(&settings_ctx.fail_msg)) goto fail;

  EvalSettingsInterface settings_interface = {
    .context = &settings_ctx,
    .state = state,
    .define_symbol = define_symbol_interface,
    .symbol_lookup = symbol_lookup_interface,
    .report_failure = report_failure_interface,
  };

  if (settings_cb != NULL) {
    snprintf(settings_ctx.fail_msg, sizeof(settings_ctx.fail_msg), "EvalSettingsCallback failed: ");
    settings_cb(&settings_interface);
    if (settings_ctx.is_failed) goto fail;
  }

  // TODO: remove later (just for testing purposes)
  if (eval_outcome.kind == EVAL_OK) {
    const Symbol *sym = symbol_lookup("argv");
    if (sym->kind == SYM_VARIABLE) {
      eval_outcome.result = (EvalResult) {
        .kind = sym->kind,
        .as.value = sym->as.variable,
      };
    } else if (sym->kind == SYM_ARRAY) {
      eval_outcome.result = (EvalResult) {
        .kind = sym->kind,
        .as.array = sym->as.array,
      };
    }
  }

  return eval_outcome;

  fail:
    EvalOutcome result = {
      .kind = EVAL_FAIL,
      .result = { .kind = SYM_EMPTY },
      .as.fail_msg = {'\0'},
    };
    snprintf(
      result.as.fail_msg,
      sizeof(result.as.fail_msg),
      "%s%s",
      fail_prefix,
      settings_ctx.fail_msg
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

typedef struct {
  char *foo;
  int argc;
  Array argv;
} SettingsState;

static void eval_settings_cb(EvalSettingsInterface *interface) {
  SettingsState *state = (SettingsState*) interface->state;

  Symbol *foo_sym = interface->define_symbol(interface, "foo", SYM_VARIABLE);
  if (foo_sym == NULL) return;
  foo_sym->as.variable = (Value) {
    .type = TYPE_STRING,
    .as.string = (String) {
      .data = state->foo,
      .length = strlen(state->foo),
    },
  };

  Symbol *argc_sym = interface->define_symbol(interface, "argc", SYM_VARIABLE);
  if (argc_sym == NULL) return;
  argc_sym->as.variable = (Value) { .type = TYPE_U32, .as.u32 = state->argc };

  Symbol *argv_sym = interface->define_symbol(interface, "argv", SYM_ARRAY);
  if (argv_sym == NULL) return;
  argv_sym->as.array = state->argv;

  // interface->report_failure(interface, "test failure report");
}

int main(int argc, char **argv) {
  char foo[] = "some test string value\0";

  Array argv_as_Array = {
    .element_type = TYPE_STRING,
    .length = argc,
    .data = malloc(sizeof(String) * argc),
  };
  if (argv_as_Array.data == NULL) goto malloc_fail;
  for (int i = 0; i < argc; ++i) {
    String arg = { .data = argv[i], .length = strlen(argv[i]) };
    ((String*) argv_as_Array.data)[i] = arg;
  }

  SettingsState state = { .foo = foo, .argc = argc, .argv = argv_as_Array };
  EvalOutcome eval_outcome = evaluate(eval_settings_cb, (void*) &state);
  // EvalOutcome eval_outcome = evaluate(NULL, NULL);

  int exit_code = 0;

  switch (eval_outcome.kind) {
    case EVAL_OK:
    case EVAL_EXIT:
      printf("Evaluation result kind: %s\n", show_SymbolKind(eval_outcome.result.kind));

      switch (eval_outcome.result.kind) {
        case SYM_EMPTY: goto done;

        case SYM_VARIABLE:
          char *shown_value = show_Value(eval_outcome.result.as.value);
          if (shown_value == NULL) {
            fprintf(stderr, "[FAIL] show_Value failed (OOM?)\n");
            exit_code = 1;
            goto done;
          }
          printf("[OK] Result: %s\n", shown_value);
          free(shown_value);
          goto done;

        case SYM_ARRAY:
          char *shown_array = show_Array(eval_outcome.result.as.array);
          if (shown_array == NULL) {
            fprintf(stderr, "[FAIL] show_Array failed (OOM?)\n");
            exit_code = 1;
            goto done;
          }
          printf("Result: %s\n", shown_array);
          free(shown_array);
          goto done;

        default:
          fprintf(stderr, "[FAIL] Unexpected result kind: %d\n", eval_outcome.result.kind);
          exit_code = 1;
          goto done;
      }

      if (eval_outcome.kind == EVAL_EXIT) {
        printf("[OK] Evaluation finished with exit code: %d\n", eval_outcome.as.exit_code);
        exit_code = eval_outcome.as.exit_code;
        goto done;
      } else {
        printf("[OK] Evaluation was okay\n");
        goto done;
      }
      break;

    case EVAL_FAIL:
      fprintf(stderr, "[FAIL] Evaluation failed: %s\n", eval_outcome.as.fail_msg);
      exit_code = 1;
      goto done;

    default:
      fprintf(stderr, "[FAIL] Unexpected EvalOutcome kind: %d\n", eval_outcome.kind);
      exit_code = 1;
      goto done;
  }

  malloc_fail:
    fprintf(stderr, "[FAIL] malloc failed (OOM?)\n");
    return 1;

  done:
    free(argv_as_Array.data);
    return exit_code;
}
