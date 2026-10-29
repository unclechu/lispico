#include <string.h>
#include <stdio.h>

#include "src/symbol.h"
#include "src/symbol-print.h"
#include "src/evaluate.h"
#include "src/expression.h"
#include "src/read-file.h"
#include "src/parse.h"

typedef struct {
  char *foo;
  int argc;
  Array argv;
} SettingsState;

static void eval_settings_cb(EvalSettingsInterface *interface) {
  SettingsState *state = (SettingsState*) interface->state;

  Symbol *argc_sym = interface->define_symbol(interface, "argc", SYM_VARIABLE);
  if (argc_sym == NULL) return;
  argc_sym->as.variable = (Value) { .type = TYPE_U32, .as.u32 = state->argc };

  Symbol *argv_sym = interface->define_symbol(interface, "argv", SYM_ARRAY);
  if (argv_sym == NULL) return;
  argv_sym->as.array = state->argv;
}

int main(int argc, char **argv) {
  int exit_code = 0;

  char parse_error[ERR_MSG_SIZE] = {'\0'};

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

  SettingsState state = { .argc = argc, .argv = argv_as_Array };

  static char source[SOURCE_CAPACITY];
  size_t source_size = 0;

  const char *source_path = argc > 1 ? argv[1] : "/dev/stdin";

  if (!read_file_contents(source_path, &source, &source_size)) {
    exit_code = 1;
    goto done;
  }

  if (!parse_source(source, source_size, &parse_error)) {
    fprintf(stderr, "Parse error: %s\n", parse_error);
    exit_code = 1;
    goto done;
  }

  EvalOutcome eval_outcome = evaluate(
    expressions,
    expression_count,
    eval_settings_cb,
    (void*) &state
  );

  switch (eval_outcome.kind) {
    case EVAL_OK:
    case EVAL_EXIT:
      if (eval_outcome.kind == EVAL_EXIT) exit_code = eval_outcome.as.exit_code;
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
    return 1;

  done:
    clean_evaluation();
    clean_symbols();
    clean_expressions();
    free(argv_as_Array.data);
    return exit_code;
}
