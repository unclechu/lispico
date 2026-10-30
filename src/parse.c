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

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse.h"
#include "number.h"

typedef struct {
  // Input text
  const char *source;
  size_t length;

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

// Report parsing failure
static bool parser_fail(Parser *parser, const char *message) {
  snprintf(
    *parser->error,
    sizeof(*parser->error),
    "%zu:%zu: %s",
    parser->line,
    parser->column,
    message
  );
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
  if (expression_count >= EXPR_CAPACITY) return parser_fail(parser, "Expression capacity exceeded");
  *index = expression_count;
  expression_count++;
  expressions[*index] = (Expression) { .kind = kind, .first_child = EXPR_NONE, .next = EXPR_NONE };
  return true;
}

// Allocate a `String` literal and register in `expression_allocations`
static bool allocate_text(Parser *parser, const char *text, size_t length, uint32_t *index) {
  size_t length_bytes = sizeof(char) * (length + 1);

  // Reuse identical immutable strings, including function and variable names.
  for (uint32_t i = 0; i < expression_allocation_count; ++i) {
    const ExpressionAllocation *allocation = &expression_allocations[i];
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
  if (!allocate_expression_data(text, length_bytes, TYPE_STRING, (uint32_t)length, index))
    return parser_fail(parser, "Unable to allocate expression text");

  return true;
}

static bool parse_expression(Parser *parser, uint32_t *index);

// Recursively parse an S-expression list `(…)`
static bool parse_list(Parser *parser, uint32_t *index) {
  if (parser->depth >= PARSER_MAX_DEPTH)
    return parser_fail(parser, "Maximum nesting depth exceeded");

  if (!new_expression(parser, EXPR_LIST, index)) return false;
  advance(parser); // Skip '('
  parser->depth++;
  uint32_t previous_child = EXPR_NONE;

  for (;;) {
    if (!skip_layout(parser)) return false;
    if (parser->position == parser->length) return parser_fail(parser, "Unterminated list");

    // Current S-expression list is closed
    if (current_char(parser) == ')') {
      advance(parser);
      parser->depth--;
      return true;
    }

    uint32_t child;

    // Recursively parse children
    if (!parse_expression(parser, &child)) return false;

    if (previous_child == EXPR_NONE) expressions[*index].first_child = child;
    else expressions[previous_child].next = child;

    previous_child = child;
  }
}

// Parse a `String` literal
static bool parse_string(Parser *parser, uint32_t *index) {
  char text[EXPR_TEXT_SIZE] = {0};
  size_t length = 0;
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

    if (length >= sizeof(text) - 1) return parser_fail(parser, "String literal is too long");
    text[length] = c;
    length++;
    advance(parser); // Skip the already consumed character
  }

  // Terminate the string before storing it in the expression allocations.
  text[length] = '\0';

  if (!new_expression(parser, EXPR_STRING, index)) return false;
  return allocate_text(parser, text, length, &expressions[*index].data);
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
      return parser_fail(parser,
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
  expressions[*index].data = data;

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

  return allocate_text(parser, text, length, &expressions[*index].data);
}

// Parse an expression
static bool parse_expression(Parser *parser, uint32_t *index) {
  if (!skip_layout(parser)) return false;

  switch (current_char(parser)) {
    case '(': return parse_list(parser, index);
    case '"': return parse_string(parser, index);
    case ')': return parser_fail(parser, "Unexpected ')'");
    case '\0':
      if (parser->position == parser->length) return parser_fail(parser, "Expected an expression");
      break;
  }

  return parse_atom(parser, index);
}

// Parse expressions from a string into global `expressions` and `expression_allocations` tables
bool parse_source(const char *source, size_t length, char (*error)[ERR_MSG_SIZE]) {
  if (error == NULL) return false;
  (*error)[0] = '\0';
  clean_expressions();

  if (source != NULL && memchr(source, '\0', length) != NULL) {
    snprintf(*error, sizeof(*error), "Source contains a null byte");
    return false;
  }

  if (source == NULL && length != 0) {
    snprintf(*error, sizeof(*error), "Source pointer is NULL");
    return false;
  }

  // Parser state
  Parser parser = {
    .source = source,
    .length = length,
    .line = 1,
    .column = 1,
    .error = error,
  };

  uint32_t previous_expression_index = EXPR_NONE;

  for (;;) {
    if (!skip_layout(&parser)) goto fail;
    if (parser.position == parser.length) return true; // Done parsing

    uint32_t current_expression_index;
    if (!parse_expression(&parser, &current_expression_index)) goto fail;

    if (previous_expression_index != EXPR_NONE)
      expressions[previous_expression_index].next = current_expression_index;

    previous_expression_index = current_expression_index;
  }

  fail:
    clean_expressions();
    return false;
}
