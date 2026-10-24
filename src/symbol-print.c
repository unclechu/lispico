// A set of functions for getting string representations of `Symbol`-related types.

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

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

// `show_String` into a specified pointer.
//
// Requires a buffer of at least (string.length + 3) chars.
// Writes '\0' terminator at the end and wraps the string into double quotes.
static void show_String_to_pointer(char *dest, String string) {
  dest[0] = '"';
  // +1 to shift quotation mark
  if (string.length > 0) memcpy(dest + 1, string.data, sizeof(char) * string.length);
  dest[string.length + 1] = '"';
  dest[string.length + 2] = '\0';
}

// Make a printable string representation of a `String`.
//
// Will wrap the string into quotation marks.
// Note that `\0` is not expected in the `String` value.
// Will return `NULL` in case of failure (e.g. `malloc`).
//
// WARNING! Caller must free the allocated resulting string!
char* show_String(String string) {
  // Plus two quotation marks and one '\0' terminator.
  size_t shown_string_length = string.length + 3;
  size_t shown_string_size = sizeof(char) * shown_string_length;
  char *shown_string = malloc(shown_string_size);
  if (shown_string == NULL) return NULL; // Likely OOM
  show_String_to_pointer(shown_string, string);
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

      // +2 for quotation marks, +2 for parentheses if `include_type`
      size_t length = type_length + string.length + 2 + (include_type ? 2 : 0);

      if (dest != NULL) {
        if (capacity <= length) return -1;
        size_t pos = 0;
        if (include_type) {
          memcpy(dest, shown_type, sizeof(char) * type_length);
          pos += type_length;
          dest[pos++] = '(';
        }
        show_String_to_pointer(dest + pos, string);
        pos += string.length + 2; // Account for quotation marks from `show_String_to_pointer`
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
// Note that `\0` is not expected in the `String` values.
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
    length += (size_t)part_length;
    // Comma and space between elements.
    if (i != 0) length += 2;
  }

  // Allocate resulting string, +1 for '\0'.
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
