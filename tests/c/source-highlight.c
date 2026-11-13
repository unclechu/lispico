#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/source-highlight.h"

static void check_highlight(
  SourceHighlightMode mode,
  const char *shell_command,
  const char *expected
) {
  const char source[] = "\t10\n20\n\t(+ 1 missing)\n";
  ExpressionSourceLocation location = {
    .byte_offset = 13,
    .line = 3,
    .column = 7,
  };

  FILE *stream = tmpfile();
  assert(stream != NULL);
  print_source_highlight(
    stream,
    source,
    strlen(source),
    &location,
    1,
    "confetti",
    mode,
    shell_command
  );

  rewind(stream);
  char result[512] = {0};
  size_t length = fread(result, 1, sizeof(result) - 1u, stream);
  assert(length == strlen(expected));
  assert(memcmp(result, expected, length) == 0);
  fclose(stream);
}

int main(void) {
  // Provide a fake bat to verify its language option independently of any
  // installed bat executable.
  char directory[] = "/tmp/lispico-source-highlight-XXXXXX";
  assert(mkdtemp(directory) != NULL);

  char bat_path[sizeof(directory) + sizeof("/bat")] = {0};
  int path_length = snprintf(bat_path, sizeof(bat_path), "%s/bat", directory);
  assert(path_length > 0 && (size_t)path_length < sizeof(bat_path));

  FILE *bat = fopen(bat_path, "w");
  assert(bat != NULL);
  assert(fputs(
    "#!/bin/sh\n"
    "previous_arg=''\n"
    "for arg do\n"
    "  if [ \"$previous_arg\" = '--language' ] && [ \"$arg\" = 'confetti' ]; then\n"
    "    printf '20\\n    (+ 1 missing)\\n'\n"
    "    exit 0\n"
    "  fi\n"
    "  previous_arg=\"$arg\"\n"
    "done\n"
    "exit 1\n",
    bat
  ) >= 0);
  assert(fclose(bat) == 0);
  assert(chmod(bat_path, 0700) == 0);
  assert(setenv("PATH", directory, 1) == 0);

  check_highlight(
    SOURCE_HIGHLIGHT_BAT,
    NULL,
    "2 | 20\x1b[0m\n"
    "3 |     (+ 1 missing)\x1b[0m\n"
    "  |          ↑\n"
  );
  check_highlight(
    SOURCE_HIGHLIGHT_SH,
    "printf 'file-type=%s\\n' \"$LISPICO_SOURCE_CONTEXT_FILE_TYPE\"",
    "file-type=confetti\n"
  );

  assert(unlink(bat_path) == 0);
  assert(rmdir(directory) == 0);

  puts("Source highlight file type tests passed");
  return 0;
}
