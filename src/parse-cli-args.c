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

// Validate variable name for the `--const NAME` or `--var NAME`
static bool valid_symbol_name(const char *name) {
  size_t length = strlen(name);
  // `empty` is a language literal, not a symbol in Lispico’s symbol table.
  return length > 0 && length < SYMBOL_NAME_SIZE && strcmp(name, "empty") != 0;
}

// Parse prepared `--var`/`--const` definition
static CliParseStatus parse_variable(
  CliOptions *options,
  const char *name,
  const char *specification,
  bool frozen,
  char *error,
  size_t error_size
) {
  // Validate variable name
  if (!valid_symbol_name(name))
    return cli_error(error, error_size, "invalid variable name for --var/--const");

  // Make sure that such variable specified only once
  for (size_t i = 0; i < options->variable_count; ++i)
    if (strcmp(options->variables[i].name, name) == 0)
      return cli_error(error, error_size, "variable “%s” specified more than once", name);

  // Find where the variable type and variable name splits
  const char *separator = strchr(specification, ':');
  if (separator == NULL)
    return cli_error(error, error_size, "variable “%s” requires TYPE:VALUE", name);

  size_t prefix_size = (size_t)(separator - specification);
  const char *contents = separator + 1; // +1 to skip `:` separator
  Value value = {0};

  // Handle strings
  if (prefix_size == 1 && specification[0] == 's') {
    value.type = TYPE_STRING;
    value.as.string = (String) { .data = contents, .length = strlen(contents) };
  }
  // Handle numeric types
  else {
    ValueType type = TYPE_VOID;
    if (prefix_size == 3 && strncmp(specification, "u32", 3) == 0) type = TYPE_U32;
    else if (prefix_size == 3 && strncmp(specification, "i32", 3) == 0) type = TYPE_I32;
    else if (prefix_size == 3 && strncmp(specification, "f32", 3) == 0) type = TYPE_F32;
    else return cli_error(error, error_size, "variable “%s” has an unsupported type", name);

    if (strcmp(contents, "true") == 0 || strcmp(contents, "false") == 0) {
      bool truth = strcmp(contents, "true") == 0;
      value.type = type;
      switch (type) {
        case TYPE_U32: value.as.u32 = truth ? 1u : 0u; break;
        case TYPE_I32: value.as.i32 = truth ? 1 : 0; break;
        case TYPE_F32: value.as.f32 = truth ? 1.0f : 0.0f; break;
        default:
          return cli_error(
            error,
            error_size,
            "variable “%s” has an unexpected resolved type (this should never happen)",
            name
          );
      }
    } else {
      NumberParseResult result = parse_number_text(
        (String) { .data = contents, .length = strlen(contents) },
        type,
        &value
      );
      if (result != NUMBER_PARSE_OK)
        return cli_error(
          error,
          error_size,
          "variable “%s” has an invalid or out-of-range numeric value",
          name
        );
    }
  }

  if (options->variable_count >= options->variable_capacity) {
    return cli_error(error, error_size, "too many command-line variables");
  }
  options->variables[options->variable_count] = (CliVariable) {
    .name = name,
    .value = value,
    .frozen = frozen,
  };
  options->variable_count++;
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
// Store the parsed source path, flags, raw argument offset, and predefined
// variables in `options`. The original `argc` and `argv` remain unchanged.
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
    .raw_args_start = argc,
  };

  if (error != NULL && error_size > 0) error[0] = '\0';
  bool source_specified = false;

  // Each variable option occupies three `argv` elements. Allocate no storage
  // when no variables are provided, and never allocate more than argc entries.
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

    // Parse a variable definition
    if (strcmp(arg, "--var") == 0 || strcmp(arg, "--const") == 0) {
      // Not enough arguments to provide the name and the type+value for the variable
      if (argc - i <= 2)
        return cli_error(error, error_size, "option “%s” requires NAME and TYPE:VALUE", arg);

      // Allocate variables array if not allocated yet
      if (options->variables == NULL) {
        // Each `--var` or `--const` definition consumes 3 `argv` elements:
        // the option, variable name, and TYPE:VALUE.
        // Exclude `argv[0]` and reserve the maximum possible number of definitions.
        // This capacity is for worst-case scenario array allocation
        // (how many variables can be possibly present with this amount of arguments).
        options->variable_capacity = ((size_t)argc - 1u) / 3u;

        if (options->variable_capacity > SIZE_MAX / sizeof(CliVariable))
          return cli_error(error, error_size, "too many command-line variables");

        options->variables = calloc(options->variable_capacity, sizeof(CliVariable));
        if (options->variables == NULL)
          return cli_error(error, error_size, "unable to allocate command-line variables");
      }

      CliParseStatus result = parse_variable(
        options,
        argv[i + 1], // Variable name
        argv[i + 2], // Variable TYPE:VALUE
        (strcmp(arg, "--const") == 0), // Is it a constant variable
        error,
        error_size
      );

      if (result != CLI_PARSE_OK) return result;
      i += 2; // Skip variable name and TYPE:VALUE arguments
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
  if (options->variables != NULL) {
    wipe_memory(options->variables, options->variable_capacity * sizeof(CliVariable));
    free(options->variables);
  }
  wipe_memory(options, sizeof(*options));
}

void print_cli_usage(FILE *stream, const char *program_name) {
  fprintf(
    stream,
    "Usage: %s [OPTIONS] [SOURCE_FILE] [-- SCRIPT_ARGS…]\n"
    "\n"
    "Options:\n"
    "  -s, --source-file FILE     Read source from FILE (or '-' for stdin)\n"
    "      --source-file=FILE     Alternative form for the source path\n"
    "  -p, --print-result         Print the final evaluation result\n"
    "      --var NAME TYPE:VAL    Define a mutable variable (repeatable)\n"
    "      --const NAME TYPE:VAL  Define a frozen variable (repeatable)\n"
    "  -h, --help                 Display this help and exit\n"
    "\n"
    "Types: u32, i32, f32, s. Numeric true/false become typed 1/0.\n"
    "Short flags may be grouped, for example “-ps FILE”; “-s” must be last.\n"
    "Only arguments after “--” are forwarded in the script’s “args” array.\n"
    "The original “argc”/“argv” remain unchanged.\n",
    (program_name != NULL && program_name[0] != '\0' ? program_name : "lispico")
  );
}
