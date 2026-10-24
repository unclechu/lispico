#include <string.h>
#include <stdio.h>

#include "src/symbol.h"
#include "src/symbol-print.h"
#include "src/evaluate.h"

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
