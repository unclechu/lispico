#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/source-location.h"

// Verify the exact excerpt and arrow, including lines with no newline at EOF.
static void check_excerpt(
  const char *source,
  uint32_t line,
  uint32_t column,
  uint32_t lines_before,
  const char *expected
) {
  const char *error_text = source;
  for (uint32_t i = 1; i < line; ++i) {
    error_text = strchr(error_text, '\n');
    assert(error_text != NULL);
    ++error_text;
  }
  assert(column > 0);
  size_t offset = (size_t)(error_text - source) + (size_t)column - 1u;
  ExpressionSourceLocation location = {
    .byte_offset = (uint32_t)offset,
    .line = line,
    .column = column,
  };

  FILE *stream = tmpfile();
  assert(stream != NULL);
  assert(print_source_location(
    stream,
    source,
    strlen(source),
    &location,
    lines_before
  ));

  rewind(stream);
  char output[4096] = {0};
  size_t length = fread(output, 1, sizeof(output) - 1u, stream);
  assert(length == strlen(expected));
  assert(memcmp(output, expected, length) == 0);
  fclose(stream);
}

int main(void) {
  const char *source = "first\nsecond\nthird\nfourth\n  (+ 1 missing)\n";
  check_excerpt(
    source, 5, 8, 3,
    "2 | second\n3 | third\n4 | fourth\n5 |   (+ 1 missing)\n  |        ↑\n"
  );
  check_excerpt(
    source, 5, 8, 1,
    "4 | fourth\n5 |   (+ 1 missing)\n  |        ↑\n"
  );
  check_excerpt(
    source, 5, 8, 0,
    "5 |   (+ 1 missing)\n  |        ↑\n"
  );
  check_excerpt(
    source, 5, 8, 100,
    "1 | first\n2 | second\n3 | third\n4 | fourth\n5 |   (+ 1 missing)\n  |        ↑\n"
  );
  check_excerpt(
    "(+ missing)", 1, 4, 3,
    "1 | (+ missing)\n  |    ↑\n"
  );
  check_excerpt(
    "one\r\n\t(+ missing)\r\n", 2, 5, 3,
    "1 | one\n2 | \t(+ missing)\n  | \t   ↑\n"
  );
  check_excerpt(
    "(println \"é\") missing", 1, 16, 0,
    "1 | (println \"é\") missing\n  |               ↑\n"
  );
  check_excerpt(
    "(println \"\x1b\") missing", 1, 16, 0,
    "1 | (println \"\\x1B\") missing\n  |                   ↑\n"
  );

  ExpressionSourceLocation invalid = { .byte_offset = 99, .line = 1, .column = 1 };
  FILE *stream = tmpfile();
  assert(stream != NULL);
  assert(!print_source_location(stream, "abc", 3, &invalid, 3));
  invalid = (ExpressionSourceLocation) { .byte_offset = 1, .line = 1, .column = 1 };
  assert(!print_source_location(stream, "abc", 3, &invalid, 3));
  assert(!print_source_location(NULL, "abc", 3, &invalid, 3));
  assert(!print_source_location(stream, NULL, 3, &invalid, 3));
  assert(!print_source_location(stream, "abc", 3, NULL, 3));
  fclose(stream);

  puts("Source location rendering tests passed");
  return 0;
}
