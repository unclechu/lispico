#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/source-registry.h"
#include "../../src/source-location.h"

int main(void) {
  const char main_source[] = "(+ 1 2)\n";
  const char included_source[] = "(println missing)\n";
  SourceFile files[2] = {0};
  SourceRegistry registry = {
    .files = files,
    .capacity = 2,
  };
  uint32_t source_id = UINT32_MAX;

  assert(register_source_file(
    &registry,
    "scripts/main.lisp",
    main_source,
    strlen(main_source),
    &source_id
  ));
  assert(source_id == 0);
  assert(register_source_file(
    &registry,
    "scripts/lib/debug.lisp",
    included_source,
    strlen(included_source),
    &source_id
  ));
  assert(source_id == 1);
  assert(registry.count == 2);
  assert(!register_source_file(&registry, "third.lisp", "", 0, &source_id));
  assert(registry.count == 2);

  const SourceFile *main_file = get_source_file(&registry, 0);
  const SourceFile *included_file = get_source_file(&registry, 1);
  assert(main_file != NULL);
  assert(included_file != NULL);
  assert(strcmp(main_file->path, "scripts/main.lisp") == 0);
  assert(main_file->source == main_source);
  assert(main_file->length == strlen(main_source));
  assert(strcmp(included_file->path, "scripts/lib/debug.lisp") == 0);
  assert(included_file->source == included_source);
  assert(included_file->length == strlen(included_source));
  assert(get_source_file(&registry, 2) == NULL);
  assert(get_source_file(NULL, 0) == NULL);

  // A location with a different source ID selects that file’s original text.
  // A future multi-file parser will fill the source IDs instead of this test.
  ExpressionSourceLocation location = {
    .source_id = 1,
    .byte_offset = 9,
    .line = 1,
    .column = 10,
  };
  FILE *stream = tmpfile();
  assert(stream != NULL);
  const SourceFile *selected = get_source_file(&registry, location.source_id);
  assert(selected == included_file);
  assert(print_source_location(
    stream,
    selected->source,
    selected->length,
    &location,
    3
  ));
  rewind(stream);
  char excerpt[256] = {0};
  size_t length = fread(excerpt, 1, sizeof(excerpt) - 1, stream);
  const char expected[] = "1 | (println missing)\n  |          ↑\n";
  assert(length == strlen(expected));
  assert(strcmp(excerpt, expected) == 0);
  fclose(stream);

  // Reject invalid registrations rather than creating unreadable entries.
  SourceRegistry invalid = { .files = files, .capacity = 2 };
  assert(!register_source_file(&invalid, NULL, "", 0, &source_id));
  assert(!register_source_file(&invalid, "test.lisp", NULL, 1, &source_id));
  assert(!register_source_file(&invalid, "test.lisp", "", 0, NULL));
  assert(invalid.count == 0);
  assert(register_source_file(&invalid, "empty.lisp", NULL, 0, &source_id));
  assert(source_id == 0);

  puts("Source registry tests passed");
  return 0;
}
