#include <string.h>
#include <stdio.h>

#include "src/symbol.h"
#include "src/symbol-print.h"
#include "src/symbol-error.h"
#include "src/evaluate.h"
#include "src/expression.h"
#include "src/read-file.h"
#include "src/parse.h"
#include "src/stdlib-print.h"
#include "src/stdlib-sleep.h"
#include "src/stdlib-env-var.h"
#include "src/stdlib-file-io.h"
#include "src/parse-cli-args.h"
#include "src/storage.h"

typedef struct {
  int argc;
  Array argv;
  Array args;
  const CliOptions *cli;
} SettingsState;

static void eval_settings_cb(EvalSettingsInterface *interface) {
  SettingsState *state = (SettingsState*) interface->state;
  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  Symbol *argc_sym = interface->define_symbol(interface, "argc", SYM_VARIABLE);
  if (argc_sym == NULL) return;
  argc_sym->as.variable = (Value) { .type = TYPE_U32, .as.u32 = state->argc };

  Symbol *argv_sym = interface->define_symbol(interface, "argv", SYM_ARRAY);
  if (argv_sym == NULL) return;
  argv_sym->as.array = state->argv;

  Symbol *args_sym = interface->define_symbol(interface, "args", SYM_ARRAY);
  if (args_sym == NULL) return;
  args_sym->as.array = state->args;

  Interpreter *interpreter = interface->interpreter;
  const char *symbol_name;
  POPULATE_STDLIB_PRINT();
  POPULATE_STDLIB_SLEEP();
  POPULATE_STDLIB_ENV_VAR();
  POPULATE_STDLIB_FILE_IO();

  // CLI definitions come last, intentionally allowing overrides of built-in
  // symbols, including `argc`, `argv`, `args` and native standard-library
  // functions.
  for (size_t i = 0; i < state->cli->variable_count; ++i) {
    const CliVariable *definition = &state->cli->variables[i];
    symbol_name = definition->name;
    Symbol *sym = interface->symbol_lookup(interface, symbol_name);
    if (sym == NULL) {
      sym = interface->define_symbol(interface, symbol_name, SYM_VARIABLE);
      if (sym == NULL) goto symbol_define_fail;
    }

    Value value = definition->value;
    if (value.type == TYPE_STRING) {
      // `argv` memory remains alive, but it must not serve as mutable storage
      // for script variables. Copy it into evaluator-owned runtime memory.
      size_t length = value.as.string.length;
      if (length >= SIZE_MAX) {
        interface->report_failure(interface, "CLI string length is too large");
        return;
      }
      char *data = allocate_runtime_data(interpreter, length + 1); // +1 for the `\0`
      if (data == NULL) {
        interface->report_failure(interface, "Unable to allocate CLI string value");
        return;
      }
      memcpy(data, value.as.string.data, length);
      data[length] = '\0';
      value.as.string.data = data;
    }

    sym->kind = SYM_VARIABLE;
    sym->as.variable = value;
    sym->frozen = definition->frozen;
  }

  return;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(interface->interpreter, &fail_msg, symbol_name);
    interface->report_failure(interface, fail_msg);
}

int main(int argc, char **argv) {
  CliOptions cli = {0};
  char parse_error[ERR_MSG_SIZE] = {'\0'};
  CliParseStatus cli_status = parse_cli_args(argc, argv, &cli, parse_error, sizeof(parse_error));

  switch (cli_status) {
    case CLI_PARSE_OK:
      break;

    case CLI_PARSE_HELP:
      print_cli_usage(stdout, argc > 0 ? argv[0] : "lispico");
      clean_cli_args(&cli);
      return 0;
      break;

    case CLI_PARSE_ERROR:
      fprintf(stderr, "lispico: %s\n\n", parse_error);
      print_cli_usage(stderr, argc > 0 ? argv[0] : "lispico");
      clean_cli_args(&cli);
      break;

    default:
      fprintf(stderr, "[FAIL] Unexpected CLI parse result status: %d\n", cli_status);
      return 1;
  }

  // Host-provided fixed-capacity interpreter storage.
  // Static storage keeps these tables off the C call stack.
  static Expression expression_storage[EXPR_CAPACITY] = {0};
  static ExpressionAllocation expression_allocation_storage[EXPR_ALLOCATION_CAPACITY] = {0};
  static Symbol symbol_storage[SYMBOL_CAPACITY] = {0};
  static EvalRuntimeAllocation runtime_allocation_storage[EVAL_RUNTIME_ALLOCATION_CAPACITY] = {0};
  static uint32_t deferred_expression_storage[EVAL_DEFER_CAPACITY] = {0};

  InterpreterStorage storage = {
    .expressions = expression_storage,
    .expression_capacity = EXPR_CAPACITY,

    .expression_allocations = expression_allocation_storage,
    .expression_allocation_capacity = EXPR_ALLOCATION_CAPACITY,

    .symbols = symbol_storage,
    .symbol_capacity = SYMBOL_CAPACITY,

    .runtime_allocations = runtime_allocation_storage,
    .runtime_allocation_capacity = EVAL_RUNTIME_ALLOCATION_CAPACITY,

    .deferred_expression_indices = deferred_expression_storage,
    .deferred_expression_capacity = EVAL_DEFER_CAPACITY,
  };

  Interpreter interpreter = {0};

  if (!configure_interpreter_storage(&interpreter, &storage)) {
    fprintf(stderr, "[FAIL] Unable to configure interpreter storage\n");
    clean_cli_args(&cli);
    return 1;
  }

  int exit_code = 0;

  Array argv_as_Array = {
    .element_type = TYPE_STRING,
    .length = argc,
    .data = argc > 0 ? (malloc(sizeof(String) * (size_t)argc)) : NULL,
  };
  if (argc > 0 && argv_as_Array.data == NULL) goto malloc_fail;
  for (int i = 0; i < argc; ++i) {
    String arg = { .data = argv[i], .length = strlen(argv[i]) };
    ((String*) argv_as_Array.data)[i] = arg;
  }

  Array args_as_Array = {
    .element_type = TYPE_STRING,
    .length = (uint32_t)(argc - cli.raw_args_start),
    .data = NULL,
  };
  if (args_as_Array.length > 0) {
    args_as_Array.data = malloc(sizeof(String) * args_as_Array.length);
    if (args_as_Array.data == NULL) goto malloc_fail;
    for (uint32_t i = 0; i < args_as_Array.length; ++i) {
      const char *arg = argv[cli.raw_args_start + (int)i];
      ((String*) args_as_Array.data)[i] = (String) { .data = arg, .length = strlen(arg) };
    }
  }

  SettingsState state = {
    .argc = argc,
    .argv = argv_as_Array,
    .args = args_as_Array,
    .cli = &cli,
  };

  static char source[SOURCE_CAPACITY];
  size_t source_size = 0;

  if (!read_file_contents(cli.source_file, &source, &source_size)) {
    exit_code = 1;
    goto done;
  }

  if (!parse_source(&interpreter, source, source_size, &parse_error)) {
    fprintf(stderr, "Parse error: %s\n", parse_error);
    exit_code = 1;
    goto done;
  }

  EvalOutcome eval_outcome = evaluate(&interpreter, eval_settings_cb, (void*) &state);

  switch (eval_outcome.kind) {
    case EVAL_OK:
    case EVAL_EXIT:
      if (eval_outcome.kind == EVAL_EXIT) exit_code = eval_outcome.as.exit_code;
      if (!cli.print_result) goto done;
      const char *shown_symbol_kind = show_SymbolKind(eval_outcome.result.kind);

      switch (eval_outcome.result.kind) {
        case SYM_EMPTY:
          printf("%s\n", shown_symbol_kind);
          goto done;

        case SYM_VARIABLE:
          char *shown_value = show_Value(eval_outcome.result.as.value);
          if (shown_value == NULL) {
            fprintf(stderr, "[FAIL] show_Value failed (OOM?)\n");
            exit_code = 1;
            goto done;
          }
          printf("%s(%s)\n", shown_symbol_kind, shown_value);
          free(shown_value);
          goto done;

        case SYM_ARRAY:
          char *shown_array = show_Array(eval_outcome.result.as.array);
          if (shown_array == NULL) {
            fprintf(stderr, "[FAIL] show_Array failed (OOM?)\n");
            exit_code = 1;
            goto done;
          }
          printf("%s(%s)\n", shown_symbol_kind, shown_array);
          free(shown_array);
          goto done;

        default:
          fprintf(stderr, "[FAIL] Unexpected result kind: %d\n", eval_outcome.result.kind);
          exit_code = 1;
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
    exit_code = 1;

  done:
    clean_evaluation(&interpreter);
    clean_symbols(&interpreter);
    clean_expressions(&interpreter);
    free(argv_as_Array.data);
    free(args_as_Array.data);
    clean_cli_args(&cli);
    return exit_code;
}
