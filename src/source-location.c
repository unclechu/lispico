#include <inttypes.h>
#include <string.h>

#include "source-location.h"

typedef struct {
  size_t first_line_start;
  size_t error_line_start;
  size_t error_offset;
  uint32_t first_line;
  int line_width;
} SourceExcerpt;

// Count decimal digits used by the displayed source line numbers.
static int line_number_width(uint32_t line) {
  int width = 1;
  while (line >= 10u) {
    line /= 10u;
    ++width;
  }
  return width;
}

// Determine the original source bytes represented by the requested excerpt.
static bool find_excerpt(
  const char *source,
  size_t source_length,
  const ExpressionSourceLocation *location,
  uint32_t lines_before,
  SourceExcerpt *excerpt
) {
  if (
    source == NULL ||
    location == NULL ||
    location->byte_offset >= source_length ||
    location->line == 0 ||
    location->column == 0
  ) return false;

  size_t error_offset = location->byte_offset;
  size_t error_line_start = error_offset;
  while (error_line_start > 0 && source[error_line_start - 1u] != '\n')
    --error_line_start;

  // Reject a mismatched location instead of pointing at the wrong character.
  if ((size_t)location->column - 1u != error_offset - error_line_start)
    return false;

  // Walk backwards over at most `lines_before` newline-separated lines.
  size_t first_line_start = error_line_start;
  uint32_t previous_lines = 0;
  while (previous_lines < lines_before && first_line_start > 0) {
    --first_line_start;
    while (first_line_start > 0 && source[first_line_start - 1u] != '\n')
      --first_line_start;
    ++previous_lines;
  }
  if (previous_lines >= location->line) return false;

  *excerpt = (SourceExcerpt) {
    .first_line_start = first_line_start,
    .error_line_start = error_line_start,
    .error_offset = error_offset,
    .first_line = location->line - previous_lines,
    .line_width = line_number_width(location->line),
  };
  return true;
}

// Format one source byte without changing the original source buffer.
static size_t display_byte(unsigned char c, char output[4]) {
  if (c == '\t') {
    memcpy(output, "    ", 4);
    return 4;
  }
  if (c < 0x20u || c == 0x7fu) {
    const char hex[] = "0123456789ABCDEF";
    output[0] = '\\';
    output[1] = 'x';
    output[2] = hex[c >> 4];
    output[3] = hex[c & 0x0fu];
    return 4;
  }
  output[0] = (char)c;
  return 1;
}

bool write_source_for_highlighting(
  FILE *stream,
  const char *source,
  size_t source_length
) {
  if (stream == NULL || source == NULL) return false;
  for (size_t i = 0; i < source_length; ++i) {
    // Preserve original line boundaries, but not the CR in CRLF endings.
    if (source[i] == '\r' && i + 1u < source_length && source[i + 1u] == '\n')
      continue;
    if (source[i] == '\n') {
      if (fputc('\n', stream) == EOF) return false;
      continue;
    }
    char shown[4];
    size_t length = display_byte((unsigned char)source[i], shown);
    if (fwrite(shown, 1, length, stream) != length) return false;
  }
  return true;
}

// Display a source excerpt with its failing line and an upward arrow.
// Source locations use byte offsets; the displayed source is never modified.
static bool print_source_excerpt(
  FILE *stream,
  const char *source,
  size_t source_length,
  const ExpressionSourceLocation *location,
  uint32_t lines_before,
  const char *highlighted,
  size_t highlighted_length
) {
  if (stream == NULL) return false;
  SourceExcerpt excerpt = {0};

  if (!find_excerpt(source, source_length, location, lines_before, &excerpt))
    return false;

  // Trust the highlighter’s returned source text; only require some output.
  if (highlighted != NULL && highlighted_length == 0) return false;

  uint32_t line = excerpt.first_line;
  size_t line_start = excerpt.first_line_start;
  size_t highlight_position = 0;

  for (;;) {
    size_t line_end = line_start;
    while (line_end < source_length && source[line_end] != '\n') ++line_end;
    // Omit the carriage return from the displayed line for CRLF input.
    size_t shown_end = line_end;
    if (shown_end > line_start && source[shown_end - 1u] == '\r') --shown_end;

    fprintf(stream, "%*" PRIu32 " | ", excerpt.line_width, line);
    if (highlighted == NULL) {
      for (size_t i = line_start; i < shown_end; ++i) {
        char shown[4];
        size_t length = display_byte((unsigned char)source[i], shown);
        fwrite(shown, 1, length, stream);
      }
    } else {
      size_t first = highlight_position;

      while (highlight_position < highlighted_length && highlighted[highlight_position] != '\n')
        ++highlight_position;

      fwrite(highlighted + first, 1, highlight_position - first, stream);

      // Always restore terminal colors before printing the gutter and arrow.
      fputs("\x1b[0m", stream);

      if (highlight_position < highlighted_length) ++highlight_position;
    }
    fputc('\n', stream);

    if (line_start == excerpt.error_line_start) {
      fprintf(stream, "%*s | ", excerpt.line_width, "");
      // Match displayed widths: four spaces for tabs or escaped controls,
      // and one space for every UTF-8 leading byte.
      for (size_t i = line_start; i < excerpt.error_offset; ++i) {
        unsigned char c = (unsigned char)source[i];

        if (c == '\t' || (c < 0x20u || c == 0x7fu))
          fputs("    ", stream);
        else if ((c & 0xc0u) != 0x80u)
          fputc(' ', stream);
      }
      fputs("↑\n", stream);
      return true;
    }
    if (line_end >= source_length) return false;
    line_start = line_end + 1u;
    ++line;
  }
}

bool print_source_location(
  FILE *stream,
  const char *source,
  size_t source_length,
  const ExpressionSourceLocation *location,
  uint32_t lines_before
) {
  return print_source_excerpt(
    stream,
    source,
    source_length,
    location,
    lines_before,
    NULL,
    0
  );
}

bool print_highlighted_source_location(
  FILE *stream,
  const char *source,
  size_t source_length,
  const ExpressionSourceLocation *location,
  uint32_t lines_before,
  const char *highlighted,
  size_t highlighted_length
) {
  if (highlighted == NULL) return false;
  return print_source_excerpt(
    stream,
    source,
    source_length,
    location,
    lines_before,
    highlighted,
    highlighted_length
  );
}
