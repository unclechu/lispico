#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/symbol-print.h"

static void expect_rendered(const char *label, char *actual, const char *expected) {
  if (actual == NULL || strcmp(actual, expected) != 0) {
    fprintf(
      stderr,
      "%s: expected [%s], got [%s]\n",
      label,
      expected,
      actual != NULL ? actual : "(null)"
    );
    free(actual);
    exit(EXIT_FAILURE);
  }
  free(actual);
}

int main(void) {
  // `str-new` produces this exact kind of string: ten NUL bytes.
  const char zero_bytes[11] = {0};
  String zero_string = { .data = zero_bytes, .length = 10 };
  expect_rendered("NUL string", show_String(zero_string), "\"\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\"");

  Value zero_value = { .type = TYPE_STRING, .as.string = zero_string };
  expect_rendered(
    "NUL value",
    show_Value(zero_value),
    "TYPE_STRING(\"\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\")"
  );

  // Escaping must not alter the underlying String, including its length.
  const char mixed_bytes[] = {'A', '\0', '\n', '\r', '\t', '\\', '"', '\x01', 'B', '\x7f', '\0'};
  String mixed_string = { .data = mixed_bytes, .length = 10 };
  const char *mixed_shown = "\"A\\0\\n\\r\\t\\\\\\\"\\x01B\\x7F\"";
  expect_rendered("escaped string", show_String(mixed_string), mixed_shown);
  Value mixed_value = { .type = TYPE_STRING, .as.string = mixed_string };
  expect_rendered(
    "escaped value",
    show_Value(mixed_value),
    "TYPE_STRING(\"A\\0\\n\\r\\t\\\\\\\"\\x01B\\x7F\")"
  );

  String empty = { .data = NULL, .length = 0 };
  expect_rendered("empty string", show_String(empty), "\"\"");
  Value empty_value = { .type = TYPE_STRING, .as.string = empty };
  expect_rendered("empty value", show_Value(empty_value), "TYPE_STRING(\"\")");

  String elements[] = { mixed_string, empty };
  Array array = { .element_type = TYPE_STRING, .length = 2, .data = elements };
  expect_rendered(
    "array of strings",
    show_Array(array),
    "ARRAY(TYPE_STRING, [\"A\\0\\n\\r\\t\\\\\\\"\\x01B\\x7F\", \"\"])"
  );

  String invalid = { .data = NULL, .length = 1 };
  if (show_String(invalid) != NULL) {
    fputs("invalid string should fail to render\n", stderr);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
