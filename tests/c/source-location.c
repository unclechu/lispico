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
    "1 | one\n2 |     (+ missing)\n  |        ↑\n"
  );
  check_excerpt(
    "a\tb\tmissing", 1, 5, 0,
    "1 | a    b    missing\n  |           ↑\n"
  );
  check_excerpt(
    "\t\tmissing", 1, 3, 0,
    "1 |         missing\n  |         ↑\n"
  );
  check_excerpt(
    "foo\tbar", 1, 4, 0,
    "1 | foo    bar\n  |    ↑\n"
  );
  check_excerpt(
    "\tfirst\nsecond\t\tmissing", 2, 9, 1,
    "1 |     first\n2 | second        missing\n  |               ↑\n"
  );
  check_excerpt(
    "(println \"é\") missing", 1, 16, 0,
    "1 | (println \"é\") missing\n  |               ↑\n"
  );
  check_excerpt(
    "(println \"\x1b\") missing", 1, 16, 0,
    "1 | (println \"\\x1B\") missing\n  |                   ↑\n"
  );

  // Prepare bat input from the original bytes, expanding tabs before bat
  // receives them. The original source and source-map offsets stay unchanged.
  FILE *prepared = tmpfile();
  assert(prepared != NULL);
  const char source_with_tabs[] = "first\r\n\t(+ 1 missing)\n";
  assert(write_source_for_highlighting(prepared, source_with_tabs, strlen(source_with_tabs)));
  rewind(prepared);
  char prepared_text[128] = {0};
  size_t prepared_length = fread(prepared_text, 1, sizeof(prepared_text) - 1u, prepared);
  assert(prepared_length == strlen("first\n    (+ 1 missing)\n"));
  assert(strcmp(prepared_text, "first\n    (+ 1 missing)\n") == 0);
  fclose(prepared);

  // The external highlighter may add ANSI SGR sequences but must not change
  // any displayed source bytes. The entire excerpt is checked before output.
  const char *colored_source = "\x1b[35mfirst\x1b[0m\n\x1b[35m    (+ 1 missing)\x1b[0m\n";
  ExpressionSourceLocation highlighted_location = {
    .byte_offset = 13,
    .line = 2,
    .column = 7,
  };
  FILE *highlighted_stream = tmpfile();
  assert(highlighted_stream != NULL);
  assert(print_highlighted_source_location(
    highlighted_stream,
    source_with_tabs,
    strlen(source_with_tabs),
    &highlighted_location,
    1,
    colored_source,
    strlen(colored_source)
  ));
  rewind(highlighted_stream);
  char colored_result[512] = {0};
  size_t colored_length = fread(colored_result, 1, sizeof(colored_result) - 1u, highlighted_stream);
  const char *expected_colored =
    "1 | \x1b[35mfirst\x1b[0m\x1b[0m\n"
    "2 | \x1b[35m    (+ 1 missing)\x1b[0m\x1b[0m\n"
    "  |          ↑\n";
  assert(colored_length == strlen(expected_colored));
  assert(memcmp(colored_result, expected_colored, colored_length) == 0);
  fclose(highlighted_stream);

  highlighted_stream = tmpfile();
  assert(highlighted_stream != NULL);
  const char *invalid_color = "first\n\x1b[2J    (+ 1 missing)\n";
  assert(!print_highlighted_source_location(
    highlighted_stream,
    source_with_tabs,
    strlen(source_with_tabs),
    &highlighted_location,
    1,
    invalid_color,
    strlen(invalid_color)
  ));
  assert(ftell(highlighted_stream) == 0);
  fclose(highlighted_stream);

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
