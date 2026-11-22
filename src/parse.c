/*
  Parser implementation.

  Take a text file and populate `expressions`/`expression_count`
  and `expression_allocations`/`expression_allocation_count`.

  Core grammar:

  program    := expression*
  expression := list | string | number | name
  list       := "(" expression* ")"
  string     := '"' characters '"'
  number     := signed integer | unsigned integer | float
  name       := nonempty atom that is not a number
*/

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "parse.h"
#include "number.h"
#include "storage.h"
#include "wipe-memory.h"

typedef struct IncludeFrame {
  dev_t device;
  ino_t inode;
  const struct IncludeFrame *previous;
} IncludeFrame;

typedef struct {
  SourceRegistry *registry;
  const IncludeFrame *active;
} IncludeContext;

typedef struct {
  Interpreter *interpreter;

  // Input text
  const char *source;
  size_t length;
  const char *path;
  bool is_included;
  uint32_t source_id;
  IncludeContext *includes; // `NULL` for the original single-source parser API.

  // Parsing character position relative to the input
  size_t position;
  // Current line of the input where the parser is currently looking at
  size_t line;
  // Current column of the line of the input where the parser is currently looking at
  size_t column;

  // S-expression current depth
  uint32_t depth;

  // Error message pointer to report error message to
  char (*error)[ERR_MSG_SIZE];
} Parser;

// A sequence of sibling expressions. An empty range has both indices set to
// EXPR_NONE. Includes return zero or more expressions here.
typedef struct {
  uint32_t first;
  uint32_t last;
} ParsedExpressions;

// Append a parsed range without introducing an additional expression or scope.
// Empty ranges leave the existing sibling chain unchanged.
static void append_expressions(
  Interpreter *interpreter,
  ParsedExpressions *sequence,
  ParsedExpressions added
) {
  if (added.first == EXPR_NONE) return;

  if (sequence->first == EXPR_NONE)
    sequence->first = added.first;
  else
    interpreter->storage.expressions[sequence->last].next = added.first;

  sequence->last = added.last;
}

// Report parsing failure
static bool parser_fail(Parser *parser, const char *message) {
  if (parser->is_included && parser->path != NULL) {
    snprintf(
      *parser->error,
      sizeof(*parser->error),
      "%s:%zu:%zu: %s",
      parser->path,
      parser->line,
      parser->column,
      message
    );
  } else {
    snprintf(
      *parser->error,
      sizeof(*parser->error),
      "%zu:%zu: %s",
      parser->line,
      parser->column,
      message
    );
  }
  return false;
}

// Read current character (or `\0` if it’s the end of input)
static char current_char(const Parser *parser) {
  return parser->position < parser->length ? parser->source[parser->position] : '\0';
}

// Check if the following input starts with specified 2 characters
static bool starts_with(const Parser *parser, char first, char second) {
  return (
    parser->position + 1 < parser->length &&
    parser->source[parser->position] == first &&
    parser->source[parser->position + 1] == second
  );
}

// Move/skip one character forward
static void advance(Parser *parser) {
  if (parser->position == parser->length) return;
  if (parser->source[parser->position] == '\n') {
    parser->line++;
    parser->column = 1;
  } else {
    parser->column++;
  }
  parser->position++;
}

// Skip spacing characters and comments
static bool skip_layout(Parser *parser) {
  for (;;) {
    unsigned char c = (unsigned char)current_char(parser);
    if (parser->position < parser->length && isspace(c)) {
      // Skip spacing character or a line break
      advance(parser);
    }
    // Skip comment till the end of the line
    else if (c == ';') {
      while (parser->position < parser->length && current_char(parser) != '\n')
        advance(parser);
    }
    // Skip block comments
    else if (starts_with(parser, '#', '|')) {
      // Block comments nesting level
      uint32_t nesting = 1;

      // Skip the comment block opening
      advance(parser);
      advance(parser);

      // Until all nested block comments are closed
      while (nesting != 0) {
        if (parser->position == parser->length)
          return parser_fail(parser, "Unterminated block comment");
        // Opening of a nested block comment
        if (starts_with(parser, '#', '|')) {
          if (nesting == UINT32_MAX) return parser_fail(parser, "Block comment nesting too deep");
          nesting++;

          // Skip the comment block opening
          advance(parser);
          advance(parser);
        }
        // Closing
        else if (starts_with(parser, '|', '#')) {
          nesting--;

          // Skip the comment block closing
          advance(parser);
          advance(parser);
        } else {
          // Skip the comment block comment character
          advance(parser);
        }
      }
    } else {
      return true;
    }
  }
}

static bool new_expression(Parser *parser, uint32_t kind, uint32_t *index) {
  Interpreter *interpreter = parser->interpreter;

  if (interpreter->expression_count >= interpreter->storage.expression_capacity)
    return parser_fail(parser, "Expression capacity exceeded");

  *index = interpreter->expression_count;
  interpreter->expression_count++;

  interpreter->storage.expressions[*index] = (Expression) {
    .kind = kind,
    .first_child = EXPR_NONE,
    .next = EXPR_NONE,
  };

  return true;
}

// Allocate a `String` literal and register in `expression_allocations`
static bool allocate_text(Parser *parser, const char *text, size_t length, uint32_t *index) {
  size_t length_bytes = sizeof(char) * (length + 1);

  // Reuse identical immutable strings, including function and variable names.
  for (uint32_t i = 0; i < parser->interpreter->expression_allocation_count; ++i) {
    const ExpressionAllocation *allocation =
      &parser->interpreter->storage.expression_allocations[i];

    if (
      allocation->data != NULL &&
      allocation->element_type == TYPE_STRING &&
      allocation->length == length &&
      // Account for `\0` terminator
      allocation->size == length_bytes &&
      memcmp(allocation->data, text, length_bytes) == 0
    ) {
      *index = i;
      return true; // Found a string match, re-using
    }
  }

  // No such string found in already allocated table, allocating new
  if (!allocate_expression_data(
    parser->interpreter,
    text,
    length_bytes,
    TYPE_STRING,
    (uint32_t)length,
    index
  ))
    return parser_fail(parser, "Unable to allocate expression text");

  return true;
}

static bool parse_expression(Parser *parser, ParsedExpressions *result);
static bool parse_sequence(Parser *parser, bool in_list, ParsedExpressions *result);

// Recursively parse an S-expression list `(…)`
static bool parse_list(Parser *parser, uint32_t *index) {
  if (parser->depth >= PARSER_MAX_DEPTH)
    return parser_fail(parser, "Maximum nesting depth exceeded");

  if (!new_expression(parser, EXPR_LIST, index)) return false;
  advance(parser); // Skip '('
  parser->depth++;
  ParsedExpressions children = { .first = EXPR_NONE, .last = EXPR_NONE };
  if (!parse_sequence(parser, true, &children)) return false;

  parser->interpreter->storage.expressions[*index].first_child = children.first;
  parser->depth--;
  return true;
}

// Parse a `String` literal
static bool parse_string_text(
  Parser *parser,
  char (*text)[EXPR_TEXT_SIZE],
  size_t *length
) {
  *length = 0;
  advance(parser); // opening quote

  for (;;) {
    if (parser->position == parser->length || current_char(parser) == '\n')
      return parser_fail(parser, "Unterminated string literal");

    char c = current_char(parser);

    // The string literal is closed
    if (c == '"') { advance(parser); break; }

    // Handle escaped characters
    if (c == '\\') {
      advance(parser); // Skip the escaping backslash

      if (parser->position == parser->length)
        return parser_fail(parser, "Unterminated escape sequence");

      switch (current_char(parser)) {
        case '"': c = '"'; break;
        case '\\': c = '\\'; break;
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case '0': c = '\0'; break;
        default: return parser_fail(parser, "Unknown string escape sequence");
      }
    }

    // -1 for excluding `\0`
    if (*length >= sizeof(*text) - 1) return parser_fail(parser, "String literal is too long");
    (*text)[*length] = c;
    (*length)++;
    advance(parser); // Skip the already consumed character
  }

  // Terminate the decoded string before storing or using it as a path.
  (*text)[*length] = '\0';
  return true;
}

static bool parse_string(Parser *parser, uint32_t *index) {
  char text[EXPR_TEXT_SIZE] = {0};
  size_t length = 0;
  if (!parse_string_text(parser, &text, &length)) return false;
  if (!new_expression(parser, EXPR_STRING, index)) return false;
  return allocate_text(
    parser,
    text,
    length,
    &parser->interpreter->storage.expressions[*index].data
  );
}

static bool is_atom_delimiter(const Parser *parser) {
  if (parser->position == parser->length) return true;
  unsigned char c = (unsigned char)current_char(parser);
  return (
    isspace(c) ||
    c == '(' ||
    c == ')' ||
    c == '"' ||
    c == ';' ||
    starts_with(parser, '#', '|')
  );
}

static bool parse_number(Parser *parser, String text, uint32_t *index) {
  Value number = {0};

  switch (parse_number_text(text, TYPE_VOID, &number)) {
    case NUMBER_PARSE_OK: break;
    case NUMBER_PARSE_INVALID: return parser_fail(parser, "Invalid numeric literal");
    case NUMBER_PARSE_INVALID_EXPONENT: return parser_fail(parser, "Invalid numeric exponent");
    case NUMBER_PARSE_OUT_OF_RANGE:
      return parser_fail(
        parser,
        strpbrk(text.data, ".eE") ? "Float literal out of range" :
        text.data[0] == '-' ? "i32 literal out of range" :
        "u32 literal out of range"
      );
    case NUMBER_PARSE_TOO_LONG: return parser_fail(parser, "Numeric literal too long");
    default: return parser_fail(parser, "Unknown numeric parsing error");
  }

  uint32_t kind;
  uint32_t data;

  switch (number.type) {
    case TYPE_U32:
      kind = EXPR_U32;
      data = number.as.u32;
      break;
    case TYPE_I32:
      kind = EXPR_I32;
      memcpy(&data, &number.as.i32, sizeof(data));
      break;
    case TYPE_F32:
      kind = EXPR_F32;
      memcpy(&data, &number.as.f32, sizeof(data));
      break;
    default:
      return parser_fail(parser, "Unexpected parsed numeric type");
  }

  if (!new_expression(parser, kind, index)) return false;
  parser->interpreter->storage.expressions[*index].data = data;

  return true;
}

// Parse an atom (see `is_atom_delimiter`)
static bool parse_atom(Parser *parser, uint32_t *index) {
  char text[EXPR_TEXT_SIZE] = {0};
  size_t length = 0;

  // Skip delimeters
  while (!is_atom_delimiter(parser)) {
    if (length >= sizeof(text) - 1) return parser_fail(parser, "Atom too long");
    text[length] = current_char(parser);
    length++;
    advance(parser);
  }

  if (length == 0) return parser_fail(parser, "Expected an expression");
  text[length] = '\0';

  String atom = { .data = text, .length = length };
  if (is_number_start(atom)) return parse_number(parser, atom, index);

  if (length >= SYMBOL_NAME_SIZE) return parser_fail(parser, "Name exceeds SYMBOL_NAME_SIZE");

  if (!new_expression(parser, EXPR_NAME, index)) return false;

  return allocate_text(
    parser,
    text,
    length,
    &parser->interpreter->storage.expressions[*index].data
  );
}

// An include is recognized before allocating an expression. A parser probe
// reads only its opening atom, preserving normal layout and comment handling.
static bool is_include(Parser *parser) {
  if (parser->includes == NULL || current_char(parser) != '(') return false;

  Parser probe = *parser;
  advance(&probe); // Skip `(`
  if (!skip_layout(&probe)) return false;
  const char keyword[] = "include";
  size_t keyword_length = sizeof(keyword) - 1;
  if (
    probe.length - probe.position < keyword_length ||
    memcmp(probe.source + probe.position, keyword, keyword_length) != 0
  ) return false;
  probe.position += keyword_length;
  return is_atom_delimiter(&probe);
}

static bool include_fail_at(
  Parser *parser,
  size_t line,
  size_t column,
  const char *message
) {
  Parser location = *parser;
  location.line = line;
  location.column = column;
  return parser_fail(&location, message);
}

// Preserve the path used to reach the containing file. Do not canonicalize:
// filesystem identity, not pathname spelling, is used to detect cycles.
//
// Construct an included file’s path relative to the containing source file.
// Absolute paths are used unchanged. If the source has no directory component
// (or comes from stdin), relative paths resolve from the current working
// directory.
//
// For example:
//   parser->path = "/home/user/scripts/main.lisp"
//   name         = "../lib/settings.lisp"
//
// The directory prefix is everything in `parser->path` up to and including
// its last '/' character, excluding the source file’s own name:
//
//   directory prefix = "/home/user/scripts/"
//   directory_length = 19 // Includes the trailing `/`.
//
// The included file’s name is counted separately:
//
//   name_length = 20  // Length of `../lib/settings.lisp`, excluding `\0`.
//
// The total allocation includes both lengths and one additional byte
// for the terminating `\0`:
//
//   Allocated bytes = 19 + 20 + 1 = 40
//
// The resulting path is:
//   /home/user/scripts/../lib/settings.lisp
static char* include_path(const Parser *parser, const char *name) {
  size_t name_length = strlen(name);
  size_t directory_length = 0;

  // Relative includes use the directory containing the current source file.
  // Absolute paths already identify their location and need no prefix.
  if (name[0] != '/' && parser->path != NULL) {
    // Find the last separator to exclude the source file’s own name.
    const char *separator = strrchr(parser->path, '/');

    // Include the separator itself (+1), so appending the included file’s
    // name produces a valid path without adding another `/`.
    if (separator != NULL) directory_length = (size_t)(separator - parser->path) + 1;
  }

  // Reserve space for the directory prefix, file name, and terminating `\0`.
  // Subtract the name length and 1 first to prevent size_t overflow.
  if (directory_length > (SIZE_MAX - name_length - 1)) return NULL;

  // The extra byte (+1) stores the terminating `\0`.
  char *path = malloc(directory_length + name_length + 1);
  if (path == NULL) return NULL;

  // Copy the containing directory, including its trailing `/`.
  if (directory_length != 0) memcpy(path, parser->path, directory_length);

  // Append the included file’s name and its terminating `\0` (+1).
  // With no directory prefix, this copies the name unchanged.
  memcpy(path + directory_length, name, name_length + 1);

  return path;
}

// Parse an included source directly into the same expression and allocation
// tables. An included source never creates a wrapper list or a new `(do)`.
static bool parse_include(Parser *parser, ParsedExpressions *result) {
  size_t include_line = parser->line;
  size_t include_column = parser->column;

  advance(parser); // '('
  if (!skip_layout(parser)) return false;

  const char keyword[] = "include";
  // Skip the `keyword`
  for (size_t i = 0; i < sizeof(keyword) - 1; ++i) advance(parser);

  if (!skip_layout(parser)) return false;

  if (current_char(parser) != '"')
    return parser_fail(parser, "(include) expects one literal string path");

  char name[EXPR_TEXT_SIZE] = {0};
  size_t name_length = 0;
  if (!parse_string_text(parser, &name, &name_length)) return false;
  if (name_length == 0 || strlen(name) != name_length)
    return parser_fail(parser, "(include) path must be nonempty and contain no null bytes");

  if (!skip_layout(parser)) return false;

  if (current_char(parser) != ')')
    return parser_fail(parser, "(include) expects exactly one literal string path");
  advance(parser); // ')'

  char *path = include_path(parser, name);
  if (path == NULL) return parser_fail(parser, "Unable to allocate include path");

  char message[ERR_MSG_SIZE] = {0};
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    snprintf(message, sizeof(message), "Unable to open include “%s”: %s", path, strerror(errno));
    goto fail;
  }

  struct stat info = {0};
  if (fstat(fileno(file), &info) != 0) {
    snprintf(message, sizeof(message), "Unable to stat include “%s”: %s", path, strerror(errno));
    goto close_fail;
  }

  // Detect circular includes.
  for (
    const IncludeFrame *frame = parser->includes->active;
    frame != NULL;
    frame = frame->previous
  ) {
    if (frame->device == info.st_dev && frame->inode == info.st_ino) {
      snprintf(message, sizeof(message), "Circular include: “%s”", path);
      goto close_fail;
    }
  }

  // Allocate included source string (+1 for the `\0`).
  char *source = malloc(SOURCE_CAPACITY + 1);
  if (source == NULL) {
    snprintf(message, sizeof(message), "Unable to allocate include source buffer");
    goto close_fail;
  }

  // Read the included source.
  size_t length = fread(source, 1, SOURCE_CAPACITY, file);
  if (ferror(file)) {
    snprintf(message, sizeof(message), "Unable to read include “%s”", path);
    goto source_fail;
  }

  // Detect too big included source file.
  if (length >= SOURCE_CAPACITY) {
    if (fgetc(file) != EOF) {
      snprintf(message, sizeof(message), "Include exceeds SOURCE_CAPACITY: “%s”", path);
      goto source_fail;
    }
    if (ferror(file)) {
      snprintf(message, sizeof(message), "Unable to read include “%s”", path);
      goto source_fail;
    }
  }

  source[length] = '\0';

  // Source files are not allowed to contain `\0`.
  if (memchr(source, '\0', length) != NULL) {
    snprintf(message, sizeof(message), "Include contains a null byte: “%s”", path);
    goto source_fail;
  }

  // Close the included source file.
  fclose(file);

  // If source mapping is enabled then register the included source in the source registry.
  uint32_t source_id = 0;
  bool retain_source = parser->interpreter->storage.expression_source_locations != NULL;
  if (retain_source) {
    if (
      parser->includes->registry == NULL ||
      !register_owned_source_file(
        parser->includes->registry,
        path,
        source,
        length,
        &source_id
      )
    ) {
      snprintf(message, sizeof(message), "Unable to register include source “%s”", path);
      goto buffer_fail;
    }
  }

  IncludeFrame frame = {
    .device = info.st_dev,
    .inode = info.st_ino,
    .previous = parser->includes->active,
  };
  IncludeContext context = {
    .registry = parser->includes->registry,
    .active = &frame,
  };
  Parser included = {
    .interpreter = parser->interpreter,
    .source = source,
    .length = length,
    .path = path,
    .is_included = true,
    .source_id = source_id,
    .includes = &context,
    .line = 1,
    .column = 1,
    .depth = parser->depth,
    .error = parser->error,
  };

  // An included script can have its own shebang, just like a main script.
  if (starts_with(&included, '#', '!')) {
    while (included.position < included.length && current_char(&included) != '\n')
      advance(&included);
  }

  // Parse the included source.
  bool success = parse_sequence(&included, false, result);

  // If source mapping is disabled then wipe the source from memory.
  if (!retain_source) {
    wipe_memory(source, length + 1);
    free(source);
    wipe_memory(path, strlen(path) + 1);
    free(path);
  }

  return success;

  source_fail:
    wipe_memory(source, length);
    free(source);
    goto close_fail;

  close_fail:
    fclose(file);
    goto fail;

  fail:
    include_fail_at(parser, include_line, include_column, message);
    wipe_memory(path, strlen(path) + 1);
    free(path);
    return false;

  buffer_fail:
    wipe_memory(source, length + 1);
    free(source);
    goto fail;
}

// Parse an expression into a sibling range. Ordinary expressions contribute
// one node; a parse-time include contributes zero or more.
static bool parse_expression(Parser *parser, ParsedExpressions *result) {
  if (!skip_layout(parser)) return false;

  // Capture the start before parsing consumes the expression’s source text.
  if (
    parser->interpreter->storage.expression_source_locations != NULL &&
    (
      parser->position > UINT32_MAX ||
      parser->line > UINT32_MAX ||
      parser->column > UINT32_MAX
    )
  )
    return parser_fail(parser, "Source location exceeds UINT32_MAX");

  uint32_t byte_offset = (uint32_t)parser->position;
  uint32_t line = (uint32_t)parser->line;
  uint32_t column = (uint32_t)parser->column;

  // Expand `(include)` in place. No directive expression is allocated.
  if (is_include(parser)) return parse_include(parser, result);

  uint32_t index = EXPR_NONE;
  bool parsed = false;
  switch (current_char(parser)) {
    case '(': parsed = parse_list(parser, &index); break;
    case '"': parsed = parse_string(parser, &index); break;
    case ')': return parser_fail(parser, "Unexpected ')'");
    case '\0':
      if (parser->position >= parser->length) return parser_fail(parser, "Expected an expression");
      parsed = parse_atom(parser, &index);
      break;
    default: parsed = parse_atom(parser, &index); break;
  }
  if (!parsed) return false;

  *result = (ParsedExpressions) { .first = index, .last = index };

  // A NULL map disables location tracking without changing expression storage.
  if (parser->interpreter->storage.expression_source_locations != NULL)
    parser->interpreter->storage.expression_source_locations[index] =
      (ExpressionSourceLocation) {
        .source_id = parser->source_id,
        .byte_offset = byte_offset,
        .line = line,
        .column = column,
      };

  return true;
}

// Parse a sequence at the program root or inside a list. Each member can
// contribute an empty, single-expression, or multi-expression sibling range.
// A list consumes its closing ')'; the program root ends at end of input.
static bool parse_sequence(Parser *parser, bool in_list, ParsedExpressions *result) {
  *result = (ParsedExpressions) { .first = EXPR_NONE, .last = EXPR_NONE };

  for (;;) {
    if (!skip_layout(parser)) return false;

    if (parser->position == parser->length) {
      if (in_list) return parser_fail(parser, "Unterminated list");
      return true;
    }

    if (in_list && current_char(parser) == ')') {
      advance(parser); // The list is closed, sequence is parsed successfully
      return true;
    }

    ParsedExpressions member = { .first = EXPR_NONE, .last = EXPR_NONE };
    if (!parse_expression(parser, &member)) return false;
    append_expressions(parser->interpreter, result, member);
  }
}

// Parse expressions from a source, resetting all expression storage before
// starting. The include context is independent of optional source mapping.
static bool parse_source_impl(
  Interpreter *interpreter,
  const char *source,
  size_t length,
  const char *path,
  IncludeContext *includes,
  char (*error)[ERR_MSG_SIZE]
) {
  if (error == NULL) return false;
  (*error)[0] = '\0';

  if (
    interpreter == NULL ||
    !interpreter->configured ||
    interpreter->storage.expressions == NULL ||
    interpreter->storage.expression_allocations == NULL
  ) {
    snprintf(*error, sizeof(*error), "Interpreter storage is not configured");
    return false;
  }

  clean_expressions(interpreter);

  if (source != NULL && memchr(source, '\0', length) != NULL) {
    snprintf(*error, sizeof(*error), "Source contains a null byte");
    return false;
  }
  if (source == NULL && length != 0) {
    snprintf(*error, sizeof(*error), "Source pointer is NULL");
    return false;
  }
  if (
    includes != NULL &&
    interpreter->storage.expression_source_locations != NULL &&
    (
      includes->registry == NULL ||
      get_source_file(includes->registry, 0) == NULL
    )
  ) {
    snprintf(*error, sizeof(*error), "Main source is not registered for source mapping");
    return false;
  }

  Parser parser = {
    .interpreter = interpreter,
    .source = source,
    .length = length,
    .path = path,
    .includes = includes,
    .line = 1,
    .column = 1,
    .error = error,
  };
  if (starts_with(&parser, '#', '!')) {
    while (parser.position < parser.length && current_char(&parser) != '\n')
      advance(&parser);
  }

  ParsedExpressions expressions = { .first = EXPR_NONE, .last = EXPR_NONE };
  if (!parse_sequence(&parser, false, &expressions)) {
    clean_expressions(interpreter);
    return false;
  }
  return true;
}

bool parse_source(
  Interpreter *interpreter,
  const char *source,
  size_t length,
  char (*error)[ERR_MSG_SIZE]
) {
  return parse_source_impl(interpreter, source, length, NULL, NULL, error);
}

bool parse_source_with_includes(
  Interpreter *interpreter,
  const char *source,
  size_t length,
  const char *source_path,
  SourceRegistry *registry,
  char (*error)[ERR_MSG_SIZE]
) {
  // For stdin, relative includes resolve against the current directory.
  const char *path = source_path;
  if (path != NULL && strcmp(path, "/dev/stdin") == 0) path = NULL;

  IncludeFrame root = {0};
  IncludeContext context = { .registry = registry };
  struct stat info = {0};
  if (source_path != NULL && stat(source_path, &info) == 0) {
    root = (IncludeFrame) { .device = info.st_dev, .inode = info.st_ino };
    context.active = &root;
  }

  return parse_source_impl(interpreter, source, length, path, &context, error);
}
