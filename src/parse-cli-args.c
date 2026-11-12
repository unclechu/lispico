#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "number.h"
#include "parse-cli-args.h"
#include "wipe-memory.h"

// Report command-line arguments parsing error
static CliParseStatus cli_error(char *error, size_t error_size, const char *format, ...) {
  if (error != NULL && error_size > 0) {
    va_list args;
    va_start(args, format);
    vsnprintf(error, error_size, format, args);
    va_end(args);
  }
  return CLI_PARSE_ERROR;
}

// Validate the name of a CLI-defined variable or array.
static bool valid_symbol_name(const char *name) {
  size_t length = strlen(name);
  // `empty` is a language literal, not a symbol in Lispico’s symbol table.
  return length > 0 && length < SYMBOL_NAME_SIZE && strcmp(name, "empty") != 0;
}

// Find a definition made earlier on the command line.
static CliDefinition* find_definition(CliOptions *options, const char *name) {
  for (size_t i = 0; i < options->definition_count; ++i)
    if (strcmp(options->definitions[i].name, name) == 0)
      return &options->definitions[i];
  return NULL;
}

// Allocate space for the maximum possible number of CLI definitions once.
// Each definition consumes at least 3 arguments; `--freeze` and `--arr-set`
// only modify definitions that were already encountered.
static CliParseStatus add_definition(
  CliOptions *options,
  int argc,
  const char *name,
  SymbolKind kind,
  CliDefinition **result,
  char *error,
  size_t error_size
) {
  if (!valid_symbol_name(name))
    return cli_error(error, error_size, "invalid command-line definition name “%s”", name);

  if (find_definition(options, name) != NULL)
    return cli_error(error, error_size, "variable “%s” specified more than once", name);

  // Allocate definitions array if not allocated yet
  if (options->definitions == NULL) {
    // Reserve enough slots for the maximum possible number of CLI definitions.
    //
    // Exclude `argv[0]`, which contains the executable name.
    // Each `--var NAME TYPE:VALUE` definition consumes exactly 3 arguments.
    // Array definitions consume at least 4 arguments:
    //   `--arr NAME TYPE COUNT VALUE…`
    //   `--arr-new NAME TYPE LENGTH`
    //
    // For example, with `argc == 7`:
    //   lispico --var x u32:1 --var y u32:2  // 2 definitions, capacity 2
    //   lispico --arr x u32 2 10 20          // 1 definition, capacity 2
    //
    // `--arr-set` and `--freeze` modify existing definitions, so they don’t
    // require additional slots. Dividing by 3 gives a safe upper bound,
    // although some allocated slots may remain unused.
    options->definition_capacity = ((size_t)argc - 1u) / 3u;

    if (options->definition_capacity > (SIZE_MAX / sizeof(CliDefinition)))
      return cli_error(error, error_size, "too many command-line definitions");

    options->definitions = calloc(options->definition_capacity, sizeof(CliDefinition));
    if (options->definitions == NULL)
      return cli_error(error, error_size, "unable to allocate command-line definitions");
  }

  if (options->definition_count >= options->definition_capacity)
    return cli_error(error, error_size, "too many command-line definitions");

  // Definition memory is preallocated, just initialize it.
  CliDefinition *definition = &options->definitions[options->definition_count];
  *definition = (CliDefinition) { .name = name, .kind = kind };
  options->definition_count++;
  *result = definition;
  return CLI_PARSE_OK;
}

// Parse a type name used by `--arr` and `--arr-new`.
static bool parse_value_type(const char *text, size_t length, ValueType *type) {
  if (length == 3 && strncmp(text, "u32", length) == 0) *type = TYPE_U32;
  else if (length == 3 && strncmp(text, "i32", length) == 0) *type = TYPE_I32;
  else if (length == 3 && strncmp(text, "f32", length) == 0) *type = TYPE_F32;
  else if (length == 1 && strncmp(text, "s", length) == 0) *type = TYPE_STRING;
  else return false;
  return true;
}

// Convert one raw argument using the requested type. String data stays borrowed
// from `argv` until the evaluator settings callback copies it.
static bool parse_cli_value(ValueType type, const char *text, Value *value) {
  *value = (Value) { .type = type };

  if (type == TYPE_STRING) {
    value->as.string = (String) { .data = text, .length = strlen(text) };
    return true;
  }

  if (strcmp(text, "true") == 0 || strcmp(text, "false") == 0) {
    bool truth = (strcmp(text, "true") == 0);
    switch (type) {
      case TYPE_U32: value->as.u32 = truth ? 1u : 0u; return true;
      case TYPE_I32: value->as.i32 = truth ? 1 : 0; return true;
      case TYPE_F32: value->as.f32 = truth ? 1.0f : 0.0f; return true;
      default: return false;
    }
  }

  return (
    parse_number_text((String) { .data = text, .length = strlen(text) }, type, value)
    == NUMBER_PARSE_OK
  );
}

// Parse unsigned decimal array lengths and indices without accepting signs,
// whitespace, separators, or overflow.
static bool parse_array_index(const char *text, uint32_t *result) {
  if (text[0] == '\0') return false;

  uint32_t number = 0;
  for (size_t i = 0; text[i] != '\0'; ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    uint32_t digit = (uint32_t)(text[i] - '0');
    if (number > ((UINT32_MAX - digit) / 10u)) return false;
    number = (number * 10u) + digit;
  }

  *result = number;
  return true;
}

// Element storage is temporarily owned by `CliOptions` during argument parsing.
static size_t array_element_size(ValueType type) {
  switch (type) {
    case TYPE_U32: return sizeof(uint32_t);
    case TYPE_I32: return sizeof(int32_t);
    case TYPE_F32: return sizeof(float);
    case TYPE_STRING: return sizeof(String);
    default: return 0;
  }
}

static void set_array_element(Array *array, uint32_t index, Value value) {
  switch (array->element_type) {
    case TYPE_U32: ((uint32_t *)array->data)[index] = value.as.u32; break;
    case TYPE_I32: ((int32_t *)array->data)[index] = value.as.i32; break;
    case TYPE_F32: ((float *)array->data)[index] = value.as.f32; break;
    case TYPE_STRING: ((String *)array->data)[index] = value.as.string; break;
    default: break;
  }
}

// Define a scalar from `--var NAME TYPE:VALUE`.
static CliParseStatus parse_variable(
  CliOptions *options,
  int argc,
  const char *name,
  const char *specification,
  char *error,
  size_t error_size
) {
  const char *separator = strchr(specification, ':');
  if (separator == NULL)
    return cli_error(error, error_size, "variable “%s” requires TYPE:VALUE", name);

  size_t prefix_size = (size_t)(separator - specification);
  const char *contents = separator + 1; // +1 to skip `:` separator
  ValueType type = TYPE_VOID;
  if (!parse_value_type(specification, prefix_size, &type))
    return cli_error(error, error_size, "variable “%s” has an unsupported type", name);

  Value value = {0};
  if (!parse_cli_value(type, contents, &value))
    return cli_error(
      error,
      error_size,
      "variable “%s” has an invalid or out-of-range numeric value",
      name
    );

  CliDefinition *definition = NULL;
  CliParseStatus status = add_definition(
    options,
    argc,
    name,
    SYM_VARIABLE,
    &definition,
    error,
    error_size
  );
  if (status != CLI_PARSE_OK) return status;
  definition->as.value = value;
  return CLI_PARSE_OK;
}

// Create either a fully populated `--arr` or a zero-initialized `--arr-new`.
static CliParseStatus parse_array(
  CliOptions *options,
  int argc,
  char *const argv[],
  int values_start,

  const char *name,
  const char *type_text,
  const char *arr_length_text,

  // true — `--arr foo u32 3 1 2 3` (values `1` `2` `3` are provided right away)
  // false — `--arr-new foo u32 3`
  bool with_values,

  char *error,
  size_t error_size
) {
  // Parse array type
  ValueType type = TYPE_VOID;
  if (!parse_value_type(type_text, strlen(type_text), &type))
    return cli_error(error, error_size, "array “%s” has an unsupported element type", name);

  // Parse array length
  uint32_t length = 0;
  if (!parse_array_index(arr_length_text, &length))
    return cli_error(error, error_size, "array “%s” has an invalid length", name);

  // Not enough arguments input for this array definition
  if (with_values && length > (uint32_t)(argc - values_start))
    return cli_error(
      error,
      error_size,
      "array “%s” has fewer elements than its declared length",
      name
    );

  // Figure out the byte size per array element needed for the parsed array element type
  size_t element_size = array_element_size(type);
  if (length != 0 && element_size > (SIZE_MAX / (size_t)length))
    return cli_error(error, error_size, "array “%s” is too large", name);

  CliDefinition *definition = NULL;
  CliParseStatus status = add_definition(
    options,
    argc,
    name,
    SYM_ARRAY,
    &definition,
    error,
    error_size
  );
  if (status != CLI_PARSE_OK) return status;

  // Allocate array items memory.
  // Allocate one byte even for empty arrays, matching `(arr-new)` behavior.
  void *data = calloc(length == 0 ? 1u : (size_t)length, length == 0 ? 1u : element_size);
  if (data == NULL) return cli_error(error, error_size, "unable to allocate array “%s”", name);

  definition->as.array = (Array) {
    .element_type = type,
    .length = length,
    .data = data,
  };

  // If values are provided right away (it is `--arr …` and not `--arr-new …`)
  // then fill the allocated array with them.
  if (with_values) {
    for (uint32_t i = 0; i < length; ++i) {
      Value value = {0};
      if (!parse_cli_value(type, argv[values_start + (int)i], &value)) {
        return cli_error(
          error,
          error_size,
          "array “%s” has an invalid or out-of-range element at index %u",
          name,
          i
        );
      }
      set_array_element(&definition->as.array, i, value);
    }
  }

  return CLI_PARSE_OK;
}

// Modify an element of a previously declared, unfrozen CLI array.
//
// `--arr-set …`
static CliParseStatus parse_array_set(
  CliOptions *options,
  const char *name,
  const char *index_text,
  const char *value_text,
  char *error,
  size_t error_size
) {
  CliDefinition *definition = find_definition(options, name);

  // Array must be already defined and not frozen at this point
  if (definition == NULL || definition->kind != SYM_ARRAY)
    return cli_error(error, error_size, "array “%s” was not previously defined", name);
  if (definition->frozen)
    return cli_error(error, error_size, "array “%s” is frozen", name);

  // The modified array item index must be within array’s length
  uint32_t index = 0;
  if (!parse_array_index(index_text, &index) || index >= definition->as.array.length)
    return cli_error(error, error_size, "array “%s” index is out of bounds", name);

  // Parse the provided value (strings are taken as-is, numerics need parsing)
  Value value = {0};
  if (!parse_cli_value(definition->as.array.element_type, value_text, &value))
    return cli_error(error, error_size, "array “%s” has an invalid element value", name);

  set_array_element(&definition->as.array, index, value);
  return CLI_PARSE_OK;
}

// Parse `--source-file` / `-s` or positional source file path
static CliParseStatus set_source_file(
  CliOptions *options,
  bool *source_specified,
  const char *path,
  char *error,
  size_t error_size
) {
  if (path == NULL || path[0] == '\0')
    return cli_error(error, error_size, "source file path cannot be empty");
  if (*source_specified)
    return cli_error(error, error_size, "source file specified more than once");
  options->source_file = strcmp(path, "-") == 0 ? "/dev/stdin" : path;
  *source_specified = true;
  return CLI_PARSE_OK;
}

// Parse the complete command-line argument vector in a single pass.
//
// Iterate over `argv` starting at index 1, skipping the executable name.
// Recognized options are processed immediately, and the loop continues
// with the next argument.
//
// Options requiring values consume additional `argv` elements by advancing
// the loop index. Short-option groups are processed character by character.
//
// A standalone `--` terminates option parsing. Everything following it
// is left untouched and its starting index is recorded in `raw_args_start`.
//
// Store the parsed source path, flags, raw argument offset, and CLI definitions
// in `options`. Definitions can be modified or frozen only after declaration.
// The original `argc` and `argv` remain unchanged.
//
// Returns:
// - `CLI_PARSE_OK` — All arguments processed, or `--` encountered.
// - `CLI_PARSE_HELP` — Help requested; no evaluation should be performed.
// - `CLI_PARSE_ERROR` — Invalid arguments; details written to `error`.
CliParseStatus parse_cli_args(
  int argc,
  char *const argv[],
  CliOptions *options,
  char *error,
  size_t error_size
) {
  if (options == NULL || argc < 0 || (argc > 0 && argv == NULL))
    return cli_error(error, error_size, "invalid command-line parser arguments");

  *options = (CliOptions) {
    .source_file = "/dev/stdin",
    .with_source_map = true,
    .raw_args_start = argc,
  };

  if (error != NULL && error_size > 0) error[0] = '\0';
  bool source_specified = false;

  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];

    // No further parsing is performed after meeting the raw arguments separator
    if (strcmp(arg, "--") == 0) {
      options->raw_args_start = i + 1; // +1 to exclude the `--` separator
      return CLI_PARSE_OK;
    }

    // Help is requested, no more parsing needed
    if (strcmp(arg, "--help") == 0) return CLI_PARSE_HELP;

    if (strcmp(arg, "--print-result") == 0) {
      options->print_result = true;
      continue;
    }

    if (strcmp(arg, "--no-source-map") == 0) {
      options->with_source_map = false;
      continue;
    }

    if (strcmp(arg, "--source-file") == 0 || strcmp(arg, "-s") == 0) {
      // Not enough arguments to provide the value for the option
      if (i + 1 >= argc)
        return cli_error(error, error_size, "option “%s” requires a file path", arg);
      ++i;
      CliParseStatus result =
        set_source_file(options, &source_specified, argv[i], error, error_size);
      if (result != CLI_PARSE_OK) return result;
      continue;
    }

    // When the option is set together with its value as one single argument
    const char prefix[] = "--source-file=";
    // -1 to exclude `\0` coming from the C string literal
    if (strncmp(arg, prefix, sizeof(prefix) - 1) == 0) {
      CliParseStatus result =
        set_source_file(options, &source_specified, arg + sizeof(prefix) - 1, error, error_size);
      if (result != CLI_PARSE_OK) return result;
      continue;
    }

    // Parse a mutable scalar definition.
    if (strcmp(arg, "--var") == 0) {
      // Not enough arguments to provide the name and the type+value for the variable
      if (argc - i <= 2)
        return cli_error(error, error_size, "option “%s” requires NAME and TYPE:VALUE", arg);

      CliParseStatus result = parse_variable(
        options,
        argc,
        argv[i + 1], // Variable name
        argv[i + 2], // Variable TYPE:VALUE
        error,
        error_size
      );

      if (result != CLI_PARSE_OK) return result;
      i += 2; // Skip variable name and TYPE:VALUE arguments
      continue;
    }

    // Dense arrays consume exactly LENGTH following elements; sparse arrays
    // are created empty and can be populated with later `--arr-set` options.
    if (strcmp(arg, "--arr") == 0 || strcmp(arg, "--arr-new") == 0) {
      // Not enough arguments to provide array name, array type, and array length
      if (argc - i <= 3)
        return cli_error(error, error_size, "option “%s” requires NAME, TYPE and LENGTH", arg);

      bool with_values = (strcmp(arg, "--arr") == 0);
      CliParseStatus result = parse_array(
        options,
        argc,
        argv,
        i + 4, // Array values are starting wat this index
        argv[i + 1], // Array name
        argv[i + 2], // Array item type
        argv[i + 3], // Array length
        with_values, // Differentiator between `--arr` or `--arr-new`
        error,
        error_size
      );
      if (result != CLI_PARSE_OK) return result;

      uint32_t length = options->definitions[options->definition_count - 1u].as.array.length;
      i += 3; // Skip NAME, TYPE and LENGTH.
      if (with_values) i += (int)length; // Skip all elements, even option-looking ones.
      continue;
    }

    if (strcmp(arg, "--arr-set") == 0) {
      if (argc - i <= 3)
        return cli_error(error, error_size, "option “%s” requires NAME, INDEX and VALUE", arg);

      CliParseStatus result = parse_array_set(
        options,
        argv[i + 1], // Array name
        argv[i + 2], // Index of array element that is modified
        argv[i + 3], // A new value for that array element
        error,
        error_size
      );
      if (result != CLI_PARSE_OK) return result;
      i += 3; // Skip NAME, INDEX and VALUE.
      continue;
    }

    if (strcmp(arg, "--freeze") == 0) {
      // Not enough input in the arguments to provide a variable name to freeze
      if (i + 1 >= argc)
        return cli_error(error, error_size, "option “%s” requires NAME", arg);

      ++i; // Skip `--freeze`, land on the name of the variable
      CliDefinition *definition = find_definition(options, argv[i]);
      if (definition == NULL)
        return cli_error(error, error_size, "cannot freeze undefined CLI variable “%s”", argv[i]);

      definition->frozen = true;
      continue;
    }

    // Handle grouped short options (e.g. `-ph` for `-p` and `-h`).
    //
    // Note that `"-\0"` is considered a valid positional `-` argument
    // meaning `/dev/stdin`.
    if (arg[0] == '-' && arg[1] != '\0') {
      // Unrecognized `--…` option
      if (arg[1] == '-')
        return cli_error(error, error_size, "unknown option “%s”", arg);

      // Validate the entire cluster before acting on any flag. In particular,
      // an option that consumes the next argv element must be the last letter.
      for (size_t j = 1; arg[j] != '\0'; ++j) {
        if (arg[j] != 'p' && arg[j] != 'h' && arg[j] != 's')
          return cli_error(error, error_size, "unknown short option “-%c”", arg[j]);
        if (arg[j] == 's' && arg[j + 1] != '\0')
          return cli_error(error, error_size, "“-s” must be last in a short-option group");
      }

      // Help doesn’t require otherwise mandatory option values or source files.
      if (strchr(arg + 1, 'h') != NULL) return CLI_PARSE_HELP;

      for (size_t j = 1; arg[j] != '\0'; ++j) {
        if (arg[j] == 'p') options->print_result = true;
        if (arg[j] == 's') {
          if (i + 1 >= argc)
            return cli_error(error, error_size, "option “-s” requires a file path");
          ++i; // Consume the following argument as the source file path.
          CliParseStatus result =
            set_source_file(options, &source_specified, argv[i], error, error_size);
          if (result != CLI_PARSE_OK) return result;
        }
      }
      continue;
    }

    // Positional argument considered a source file path
    CliParseStatus result = set_source_file(options, &source_specified, arg, error, error_size);
    if (result != CLI_PARSE_OK) return result;
  }

  return CLI_PARSE_OK;
}

void clean_cli_args(CliOptions *options) {
  if (options == NULL) return;
  if (options->definitions != NULL) {
    // Wipe and free allocated `Array`s
    for (size_t i = 0; i < options->definition_count; ++i) {
      CliDefinition *definition = &options->definitions[i];
      if (definition->kind != SYM_ARRAY || definition->as.array.data == NULL) continue;

      // Intentionally not freeing `String`s of the `Array` becase CLI parser
      // does not own them, the `String` values are pointers to the strings
      // of `argv`.
      size_t element_size = array_element_size(definition->as.array.element_type);
      size_t bytes = (size_t)definition->as.array.length * element_size;
      wipe_memory(definition->as.array.data, bytes == 0 ? 1u : bytes);
      free(definition->as.array.data);
    }

    wipe_memory(options->definitions, options->definition_capacity * sizeof(CliDefinition));
    free(options->definitions);
  }
  wipe_memory(options, sizeof(*options));
}

void print_cli_usage(FILE *stream, const char *program_name) {
  fprintf(
    stream,
    "Usage: %s [OPTIONS] [SOURCE_FILE] [-- SCRIPT_ARGS…]\n"
    "\n"
    "Options:\n"
    "  -s, --source-file FILE        Read source from FILE (or “-” for stdin)\n"
    "      --source-file=FILE        Alternative form for the source path\n"
    "  -p, --print-result            Print the final evaluation result\n"
    "      --no-source-map           Use expression indices instead of line and column\n"
    "      --var NAME TYPE:VAL       Define a mutable variable (repeatable)\n"
    "      --arr NAME TYPE N VALUE…  Define an array from N arguments\n"
    "      --arr-new NAME TYPE N     Define a zero-initialized array of length N\n"
    "      --arr-set NAME IDX VAL    Set an element of a previously defined array\n"
    "      --freeze NAME             Freeze a previously defined variable or array\n"
    "  -h, --help                    Display this help and exit\n"
    "\n"
    "Types: u32, i32, f32, s. Numeric true/false become typed 1/0.\n"
    "Array values are separate arguments; --freeze prevents later --arr-set.\n"
    "Short flags may be grouped, for example “-ps FILE”; “-s” must be last.\n"
    "Only arguments after “--” are forwarded in the script’s “args” array.\n"
    "The original “argc”/“argv” remain unchanged.\n",
    (program_name != NULL && program_name[0] != '\0' ? program_name : "lispico")
  );
}
