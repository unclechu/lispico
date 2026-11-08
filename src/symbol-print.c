// A set of functions for getting string representations of `Symbol`-related types.

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>

#include "symbol-print.h"

// Get a string representation of `SymbolKind` value
const char* show_SymbolKind(SymbolKind kind) {
  switch (kind) {
    case SYM_EMPTY: return "SYM_EMPTY";
    case SYM_VARIABLE: return "SYM_VARIABLE";
    case SYM_ARRAY: return "SYM_ARRAY";
    case SYM_LABEL: return "SYM_LABEL";
    case SYM_NATIVE_FUNCTION: return "SYM_NATIVE_FUNCTION";
    default: return "( ! Unexpected SymbolKind for show_SymbolKind ! )";
  }
}

// Get a string representation of `ValueType` value
const char* show_ValueType(ValueType type) {
  switch (type) {
    case TYPE_VOID: return "TYPE_VOID";
    case TYPE_U32: return "TYPE_U32";
    case TYPE_I32: return "TYPE_I32";
    case TYPE_F32: return "TYPE_F32";
    case TYPE_STRING: return "TYPE_STRING";
    default: return "( ! Unexpected ValueType for show_ValueType ! )";
  }
}

// Render a quoted String, escaping embedded NULs and other special bytes.
//
// When `dest` is `NULL`, calculate the required length (excluding the terminator).
// Otherwise, `dest` must have room for that length plus the `\0` terminator.
// Preserves UTF-8 bytes while escaping ASCII control characters.
static bool show_String_to_pointer(char *dest, String string, size_t *length) {
  if (string.length != 0 && string.data == NULL) return false;

  static const char hex[] = "0123456789ABCDEF";
  size_t pos = 1; // Opening quotation mark
  if (dest != NULL) dest[0] = '"';

  for (size_t i = 0; i < string.length; ++i) {
    unsigned char byte = (unsigned char)string.data[i];
    char escaped[4];
    size_t escaped_length = 1;

    switch (byte) {
      // Render some special chars as common C-escapes (e.g. `\0` for the NUL).
      case '\0': escaped[0] = '\\'; escaped[1] = '0'; escaped_length = 2; break;
      case '\n': escaped[0] = '\\'; escaped[1] = 'n'; escaped_length = 2; break;
      case '\r': escaped[0] = '\\'; escaped[1] = 'r'; escaped_length = 2; break;
      case '\t': escaped[0] = '\\'; escaped[1] = 't'; escaped_length = 2; break;
      case '"': escaped[0] = '\\'; escaped[1] = '"'; escaped_length = 2; break;
      case '\\': escaped[0] = '\\'; escaped[1] = '\\'; escaped_length = 2; break;
      default:
        // If it is a special character < 0x20
        // (accounting for DEL 0x7F special character)
        // then encode it as `\xFF` form where FF is ASCII hex position.
        if (byte < 0x20 || byte == 0x7F) {
          escaped[0] = '\\';
          escaped[1] = 'x';
          escaped[2] = hex[byte >> 4];
          escaped[3] = hex[byte & 0x0F];
          escaped_length = 4;
        } else {
          // Regular ASCII character
          escaped[0] = (char)byte;
        }
        break;
    }

    // Leave space for the closing quotation mark and the terminator.
    if (pos > SIZE_MAX - escaped_length - 2) return false;
    if (dest != NULL) memcpy(dest + pos, escaped, escaped_length);
    pos += escaped_length;
  }

  if (dest != NULL) {
    dest[pos] = '"';
    dest[pos + 1] = '\0';
  }

  *length = pos + 1;
  return true;
}

// Make a printable string representation of a `String`.
//
// Wraps the string into quotation marks and escapes nonprintable bytes.
// Will return `NULL` in case of failure (e.g. `malloc`).
//
// WARNING! Caller must free the allocated resulting string!
char* show_String(String string) {
  size_t shown_string_length;
  if (!show_String_to_pointer(NULL, string, &shown_string_length)) return NULL;
  if (shown_string_length >= SIZE_MAX) return NULL;
  char *shown_string = malloc(shown_string_length + 1);
  if (shown_string == NULL) return NULL; // Likely OOM
  if (!show_String_to_pointer(shown_string, string, &shown_string_length)) {
    free(shown_string);
    return NULL;
  }
  return shown_string;
}

// Writes a `Value` into the provided buffer.
//
// When `dest == NULL` and `capacity == 0`, only calculates
// the required number of characters (excluding '\0').
//
// Returns the number of characters, or -1 on failure.
static int show_Value_to_pointer(char *dest, size_t capacity, Value value, bool include_type) {
  const char *shown_type = show_ValueType(value.type);
  switch (value.type) {
    case TYPE_VOID: return include_type
      ? snprintf(dest, capacity, "%s(void)", shown_type)
      : snprintf(dest, capacity, "void");
    case TYPE_U32: return include_type
      ? snprintf(dest, capacity, "%s(%" PRIu32 ")", shown_type, value.as.u32)
      : snprintf(dest, capacity, "%" PRIu32, value.as.u32);
    case TYPE_I32: return include_type
      ? snprintf(dest, capacity, "%s(%" PRId32 ")", shown_type, value.as.i32)
      : snprintf(dest, capacity, "%" PRId32, value.as.i32);
    case TYPE_F32: return include_type
      ? snprintf(dest, capacity, "%s(%.6f)", shown_type, value.as.f32)
      : snprintf(dest, capacity, "%.6f", value.as.f32);
    case TYPE_STRING: {
      String string = value.as.string;
      size_t type_length = include_type ? strlen(shown_type) : 0;

      size_t string_length;
      if (!show_String_to_pointer(NULL, string, &string_length)) return -1;

      // +2 for the parentheses when including the value type.
      size_t overhead = type_length + (include_type ? 2 : 0);
      if (string_length > (size_t)INT_MAX - overhead) return -1;
      size_t length = overhead + string_length;

      if (dest != NULL) {
        if (capacity <= length) return -1;
        size_t pos = 0;
        if (include_type) {
          memcpy(dest, shown_type, sizeof(char) * type_length);
          pos += type_length;
          dest[pos++] = '(';
        }
        if (!show_String_to_pointer(dest + pos, string, &string_length)) return -1;
        pos += string_length;
        if (include_type) dest[pos++] = ')'; // Overwrites `\0` from `show_String_to_pointer`
        dest[pos] = '\0';
      }

      return (int)length;
    }
    default: return snprintf(dest, capacity, "( ! Unexpected ValueType for show_Value ! )");
  }
}

// Make a printable string representation of a `Value`.
//
// Will return `NULL` in case of failure (e.g. `malloc`).
//
// WARNING! Caller must free the allocated resulting string!
char* show_Value(Value value) {
  int length = show_Value_to_pointer(NULL, 0, value, true);
  if (length < 0) return NULL;
  size_t size = sizeof(char) * ((size_t)length + 1); // +1 for `\0`.
  char *shown = malloc(size);
  if (shown == NULL) return NULL; // Likely OOM
  if (show_Value_to_pointer(shown, size, value, true) < 0) {
    free(shown);
    return NULL;
  }
  return shown;
}

// Write `Array` element (taken by its index) into `Value` pointer.
static bool write_Array_item_to_Value(const Array *array, uint32_t index, Value *value) {
  value->type = array->element_type;
  switch (array->element_type) {
    case TYPE_VOID: return true; // Void does not have associated value
    case TYPE_U32:
      value->as.u32 = ((const uint32_t*)array->data)[index];
      return true;
    case TYPE_I32:
      value->as.i32 = ((const int32_t*)array->data)[index];
      return true;
    case TYPE_F32:
      value->as.f32 = ((const float*)array->data)[index];
      return true;
    case TYPE_STRING:
      value->as.string = ((const String*)array->data)[index];
      return true;
    default: return false; // Unexpected type ()
  }
}

// Make a printable string representation of an `Array` value.
//
// String elements are escaped, including embedded NUL bytes.
// Will return `NULL` in case of failure (e.g. `malloc` or `snprintf` error).
//
// WARNING! Caller must free the allocated resulting string!
char* show_Array(Array array) {
  const char *shown_type = show_ValueType(array.element_type);

  // Calculate prefix length.
  const int prefix_length = snprintf(NULL, 0, "ARRAY(%s, [", shown_type);
  if (prefix_length < 0) return NULL;

  // Include two closing characters: "])".
  size_t length = (size_t)prefix_length + 2;

  // Calculate memory required for all elements.
  for (uint32_t i = 0; i < array.length; ++i) {
    Value element;
    if (!write_Array_item_to_Value(&array, i, &element)) return NULL;
    int part_length = show_Value_to_pointer(NULL, 0, element, false);
    if (part_length < 0) return NULL;
    if (i != 0) {
      if (length > SIZE_MAX - 2) return NULL;
      length += 2; // Comma and space between elements.
    }
    if ((size_t)part_length > SIZE_MAX - length) return NULL;
    length += (size_t)part_length;
  }

  // Allocate resulting string, +1 for '\0'.
  if (length == SIZE_MAX) return NULL;
  const size_t size = sizeof(char) * (length + 1);
  char *shown = malloc(size);
  if (shown == NULL) return NULL; // Likely OOM

  // Write prefix.
  int written = snprintf(shown, size, "ARRAY(%s, [", shown_type);

  if (written < 0 || (size_t)written >= size) {
    free(shown);
    return NULL;
  }

  size_t pos = (size_t)written;

  // Write array elements directly into allocated memory.
  for (uint32_t i = 0; i < array.length; ++i) {
    Value element;
    if (!write_Array_item_to_Value(&array, i, &element)) {
      free(shown);
      return NULL;
    }

    // Add separator
    if (i != 0) {
      const char separator[] = ", ";
      const size_t separator_length = strlen(separator);
      // Not include `\0` terminator (automatically added to the string literal).
      // So not taking `sizeof(separator)` because of this.
      memcpy(shown + pos, separator, sizeof(char) * separator_length);
      pos += separator_length;
    }

    int part_length = show_Value_to_pointer(shown + pos, size - pos, element, false);
    if (part_length < 0) {
      free(shown);
      return NULL;
    }

    pos += (size_t)part_length;
  }

  // Write closing brackets and '\0' (string literals are terminated).
  const char suffix[] = "])";
  memcpy(shown + pos, suffix, sizeof(suffix));

  return shown;
}
