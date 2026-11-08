#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "stdlib-show.h"
#include "stdlib-validation-utils.h"
#include "symbol-print.h"
#include "evaluate.h"
#include "wipe-memory.h"

// (show VALUE) — Return the executable’s debug representation as a Lispico
// string, rather than printing it to stdout.
Datum std_show(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "show";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;

  const char *shown_kind = show_SymbolKind(args[0].kind);
  char *shown_value = NULL; // malloc-owned temporary from `show_Value`/`show_Array`
  char label_text[32] = {'\0'};
  const char *value_text = NULL;
  bool has_value = false;

  switch (args[0].kind) {
    case SYM_EMPTY:
      break;

    case SYM_VARIABLE:
      shown_value = show_Value(args[0].as.value);
      if (shown_value == NULL) goto render_failed;
      value_text = shown_value;
      has_value = true;
      break;

    case SYM_ARRAY:
      shown_value = show_Array(args[0].as.array);
      if (shown_value == NULL) goto render_failed;
      value_text = shown_value;
      has_value = true;
      break;

    case SYM_LABEL: {
      // A label’s value is the expression index of its destination.
      int n = snprintf(label_text, sizeof(label_text), "%" PRIu32, args[0].as.instruction_index);
      if (n < 0 || (size_t)n >= sizeof(label_text)) goto render_failed;
      value_text = label_text;
      has_value = true;
      break;
    }

    default:
      snprintf(fail_msg, sizeof(fail_msg), "(%s) unsupported symbol kind", fn_name);
      goto fail;
  }

  size_t kind_length = strlen(shown_kind);
  size_t value_length = has_value ? strlen(value_text) : 0;
  size_t suffix_length = has_value ? 2 : 0; // Opening and closing parentheses

  // Reserve one byte for the trailing NUL.
  size_t max_content_length = SIZE_MAX - suffix_length - 1;
  if (
    kind_length > max_content_length ||
    value_length > max_content_length - kind_length
  ) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) rendered string is too large", fn_name);
    goto fail;
  }

  size_t length = kind_length + suffix_length + value_length;
  char *data = allocate_runtime_data(interpreter, length + 1); // +1 for `\0`
  if (data == NULL) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) runtime allocation failed (OOM or allocation capacity reached)",
      fn_name
    );
    goto fail;
  }

  memcpy(data, shown_kind, kind_length); // Print kind
  if (has_value) {
    data[kind_length] = '(';
    memcpy(data + kind_length + 1, value_text, value_length); // Print value
    data[length - 1] = ')';
  }
  data[length] = '\0';

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_STRING,
      .as.string = { .data = data, .length = length },
    },
  };

  if (shown_value != NULL) {
    wipe_memory(shown_value, strlen(shown_value) + 1);
    free(shown_value);
  }

  return return_value;

  render_failed:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) unable to render value (invalid data or OOM)",
      fn_name
    );
    goto fail;

  fail:
    if (shown_value != NULL) {
      wipe_memory(shown_value, strlen(shown_value) + 1);
      free(shown_value);
    }
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
