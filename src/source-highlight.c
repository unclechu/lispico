#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "source-highlight.h"
#include "source-location.h"

bool parse_source_highlight_mode(const char *name, SourceHighlightMode *mode) {
  if (name == NULL || mode == NULL) return false;
  if (strcmp(name, "none") == 0) *mode = SOURCE_HIGHLIGHT_NONE;
  else if (strcmp(name, "bat") == 0) *mode = SOURCE_HIGHLIGHT_BAT;
  else if (strcmp(name, "sh") == 0) *mode = SOURCE_HIGHLIGHT_SH;
  else return false;
  return true;
}

// Wait until the child has exited; an interrupted wait is not a failure.
static bool wait_for_child(pid_t child, int *status) {
  for (;;) {
    pid_t result = waitpid(child, status, 0);
    if (result == child) return true;
    if (result < 0 && errno == EINTR) continue;
    return false;
  }
}

// Read the bat result only after its process exits. Temporary files avoid
// stdin/stdout pipe deadlocks without requiring a background I/O loop.
static bool read_highlighted_output(FILE *output, char **text, size_t *length) {
  if (fseek(output, 0, SEEK_END) != 0) return false;
  long file_length = ftell(output);
  if (file_length < 0 || fseek(output, 0, SEEK_SET) != 0) return false;

  // Source files are bounded by Lispico’s source capacity; an unexpectedly
  // enormous highlighter result is not useful as an error excerpt.
  const size_t max_output = 16u * 1024u * 1024u;
  if ((size_t)file_length > max_output) return false;

  char *buffer = malloc((size_t)file_length + 1u);
  if (buffer == NULL) return false;
  size_t count = fread(buffer, 1, (size_t)file_length, output);
  if (count != (size_t)file_length) {
    free(buffer);
    return false;
  }
  buffer[count] = '\0';
  *text = buffer;
  *length = count;
  return true;
}

// bat is a syntax highlighter only: Lispico still writes its own line-number
// gutter and arrow. Pass source with expanded tabs so bat never sees tabs.
static bool print_with_bat(
  FILE *stream,
  const char *source,
  size_t source_length,
  const ExpressionSourceLocation *location,
  uint32_t lines_before
) {
  FILE *input = tmpfile();
  if (input == NULL) return false;
  FILE *output = tmpfile();
  if (output == NULL) {
    fclose(input);
    return false;
  }

  bool ok = false;
  if (!write_source_for_highlighting(input, source, source_length)) goto done;
  if (fflush(input) != 0 || fseek(input, 0, SEEK_SET) != 0) goto done;

  uint32_t first_line = (location->line > lines_before) ? (location->line - lines_before) : 1u;
  char line_range[80] = {0};
  snprintf(
    line_range,
    sizeof(line_range),
    "--line-range=%" PRIu32 ":%" PRIu32,
    first_line,
    location->line
  );

  pid_t child = fork();
  if (child < 0) goto done;
  if (child == 0) {
    if (
      dup2(fileno(input), STDIN_FILENO) < 0 ||
      dup2(fileno(output), STDOUT_FILENO) < 0
    ) _exit(127);

    int discarded = open("/dev/null", O_WRONLY);
    if (discarded < 0 || dup2(discarded, STDERR_FILENO) < 0) _exit(127);
    close(discarded);

    char *const arguments[] = {
      "bat",
      "--language=lisp",
      "--color=always",
      "--paging=never",
      "--style=plain",
      "--wrap=never",
      line_range,
      "-",
      NULL,
    };
    execvp("bat", arguments);
    _exit(127);
  }

  int status = 0;
  if (!wait_for_child(child, &status)) goto done;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) goto done;

  char *highlighted = NULL;
  size_t highlighted_length = 0;
  if (!read_highlighted_output(output, &highlighted, &highlighted_length)) goto done;

  ok = print_highlighted_source_location(
    stream,
    source,
    source_length,
    location,
    lines_before,
    highlighted,
    highlighted_length
  );
  free(highlighted);

  done:
    fclose(output);
    fclose(input);
    return ok;
}

// Inject metadata into the child environment without modifying the main
// process. The original source is passed unchanged through standard input.
static bool set_source_context_environment(
  const ExpressionSourceLocation *location,
  uint32_t lines_before
) {
  uint32_t first_line = (location->line > lines_before) ? (location->line - lines_before) : 1u;

  struct {
    const char *name;
    uint32_t number;
  } values[] = {
    { "LISPICO_SOURCE_CONTEXT_BEFORE", lines_before },
    { "LISPICO_SOURCE_CONTEXT_LINE", location->line },
    { "LISPICO_SOURCE_CONTEXT_COLUMN", location->column },
    { "LISPICO_SOURCE_CONTEXT_BYTE_OFFSET", location->byte_offset },
    { "LISPICO_SOURCE_CONTEXT_FIRST_LINE", first_line },
  };

  for (size_t i = 0; i < (sizeof(values) / sizeof(values[0])); ++i) {
    char text[32] = {0};
    snprintf(text, sizeof(text), "%" PRIu32, values[i].number);
    if (setenv(values[i].name, text, 1) != 0) return false;
  }
  return true;
}

// sh mode executes the user’s shell expression with system(). A child process
// isolates its environment and stdin from Lispico. Its stdout is directed to
// the diagnostic stream, and its stderr is left inherited and unsuppressed.
static void print_with_shell(
  FILE *stream,
  const char *source,
  size_t source_length,
  const ExpressionSourceLocation *location,
  uint32_t lines_before,
  const char *command
) {
  FILE *input = tmpfile();
  if (input == NULL) {
    fprintf(stream, "[WARN] Source highlighter command could not create input\n");
    return;
  }
  if (
    fwrite(source, 1, source_length, input) != source_length ||
    fflush(input) != 0 ||
    fseek(input, 0, SEEK_SET) != 0
  ) {
    fprintf(stream, "[WARN] Source highlighter command could not prepare input\n");
    fclose(input);
    return;
  }

  fflush(stream); // Print the preceding diagnostic before the child’s output.
  pid_t child = fork();
  if (child < 0) {
    fprintf(stream, "[WARN] Source highlighter command could not start\n");
    fclose(input);
    return;
  }
  if (child == 0) {
    if (
      dup2(fileno(input), STDIN_FILENO) < 0 ||
      dup2(fileno(stream), STDOUT_FILENO) < 0 ||
      !set_source_context_environment(location, lines_before)
    ) _exit(126);

    int status = system(command);
    if (status < 0) _exit(126);
    if (WIFEXITED(status)) _exit(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) _exit(128 + WTERMSIG(status));
    _exit(126);
  }

  int status = 0;
  if (!wait_for_child(child, &status)) {
    fprintf(stream, "[WARN] Unable to wait for source highlighter command\n");
  } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
    fprintf(
      stream,
      "[WARN] Source highlighter command exited with status %d\n",
      WEXITSTATUS(status)
    );
  } else if (WIFSIGNALED(status)) {
    fprintf(
      stream,
      "[WARN] Source highlighter command terminated by signal %d\n",
      WTERMSIG(status)
    );
  }
  fclose(input);
}

void print_source_highlight(
  FILE *stream,
  const char *source,
  size_t source_length,
  const ExpressionSourceLocation *location,
  uint32_t lines_before,
  SourceHighlightMode mode,
  const char *shell_command
) {
  if (stream == NULL || source == NULL || location == NULL) return;

  if (mode == SOURCE_HIGHLIGHT_SH) {
    if (shell_command != NULL) {
      print_with_shell(
        stream,
        source,
        source_length,
        location,
        lines_before,
        shell_command
      );
    }
    return;
  }

  if (
    mode == SOURCE_HIGHLIGHT_BAT &&
    print_with_bat(stream, source, source_length, location, lines_before)
  ) return;

  print_source_location(
    stream,
    source,
    source_length,
    location,
    lines_before
  );
}
