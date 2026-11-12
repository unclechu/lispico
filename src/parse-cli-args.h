#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "symbol.h"

// CLI definitions are collected before evaluation. Names and string contents
// initially refer to the process `argv` strings, which remain alive in `main()`.
// Arrays own temporary element storage; the executable copies all arrays and
// strings into evaluator-owned storage before evaluating the source.
typedef struct {
  const char *name;
  SymbolKind kind; // Either `SYM_VARIABLE` or `SYM_ARRAY`.
  bool frozen;
  union {
    Value value;
    Array array;
  } as;
} CliDefinition;

typedef struct {
  const char *source_file; // Defaults to `/dev/stdin`.
  bool print_result;
  bool with_source_map; // Enabled by default; disabled by `--no-source-map`.
  uint32_t source_context_before; // Number of preceding source lines (default: 3).
  const char *source_highlight; // NULL means environment setting or default “bat”.
  const char *source_highlight_sh; // NULL means environment setting, when needed.
  int raw_args_start; // Index immediately after `--`, or `argc` if absent.

  CliDefinition *definitions;
  size_t definition_count;
  size_t definition_capacity;
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
