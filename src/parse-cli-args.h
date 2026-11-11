#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "symbol.h"

// CLI-only values are parsed before evaluation. Names and string contents
// initially refer to the process `argv` strings, which remain alive in `main()`.
// The executable copies string values into evaluator-owned storage later.
typedef struct {
  const char *name;
  Value value;
  bool frozen;
} CliVariable;

typedef struct {
  const char *source_file; // Defaults to `/dev/stdin`.
  bool print_result;
  int raw_args_start; // Index immediately after `--`, or `argc` if absent.

  CliVariable *variables;
  size_t variable_count;
  size_t variable_capacity;
} CliOptions;

typedef enum {
  CLI_PARSE_OK = 0,
  CLI_PARSE_HELP,
  CLI_PARSE_ERROR,
} CliParseStatus;

CliParseStatus parse_cli_args(
  int argc,
  char *const argv[],
  CliOptions *options,
  char *error,
  size_t error_size
);

void clean_cli_args(CliOptions *options);
void print_cli_usage(FILE *stream, const char *program_name);
