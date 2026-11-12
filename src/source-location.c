#include <inttypes.h>

#include "source-location.h"

// Count decimal digits used by the displayed source line numbers.
static int line_number_width(uint32_t line) {
  int width = 1;
  while (line >= 10u) {
    line /= 10u;
    ++width;
  }
  return width;
}

// Display a source excerpt with its failing line and an upward arrow.
// The source map uses byte offsets. Printable UTF-8 text is kept as-is,
// while ASCII control characters are escaped to avoid terminal control codes.
bool print_source_location(
  FILE *stream,
  const char *source,
  size_t source_length,
  const ExpressionSourceLocation *location,
  uint32_t lines_before
) {
  if (
    stream == NULL ||
    source == NULL ||
    location == NULL ||
    location->byte_offset >= source_length ||
    location->line <= 0 ||
    location->column <= 0
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
    --first_line_start; // Skip the newline preceding the current line.
    while (first_line_start > 0 && source[first_line_start - 1u] != '\n')
      --first_line_start;
    ++previous_lines;
  }
  if (previous_lines >= location->line) return false;

  uint32_t line = location->line - previous_lines;
  int width = line_number_width(location->line);
  size_t line_start = first_line_start;

  for (;;) {
    size_t line_end = line_start;
    while (line_end < source_length && source[line_end] != '\n') ++line_end;

    // Omit the carriage return from the displayed line for CRLF input.
    size_t shown_end = line_end;
    if (shown_end > line_start && source[shown_end - 1u] == '\r') --shown_end;

    fprintf(stream, "%*" PRIu32 " | ", width, line);
    for (size_t i = line_start; i < shown_end; ++i) {
      unsigned char c = (unsigned char)source[i];
      if ((c < 0x20u && c != '\t') || c == 0x7fu)
        fprintf(stream, "\\x%02X", c);
      else
        fputc(c, stream);
    }
    fputc('\n', stream);

    if (line_start == error_line_start) {
      fprintf(stream, "%*s | ", width, "");
      // Preserve tab stops, account for escaped control bytes, and skip
      // UTF-8 continuation bytes when aligning ordinary single-width text.
      for (size_t i = line_start; i < error_offset; ++i) {
        unsigned char c = (unsigned char)source[i];
        if (c == '\t') fputc('\t', stream);
        else if ((c < 0x20u && c != '\t') || c == 0x7fu)
          fputs("    ", stream); // The corresponding source byte is shown as \xNN.
        else if ((c & 0xc0u) != 0x80u) fputc(' ', stream);
      }
      fputs("↑\n", stream);
      return true;
    }

    if (line_end >= source_length) return false;
    line_start = line_end + 1u;
    ++line;
  }
}
