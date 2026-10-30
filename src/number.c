#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "number.h"
#include "wipe-memory.h"

// Is this a start of a number.
//
// Assuming `text.data != NULL`.
bool is_number_start(String text) {
  if (text.length < 1) return false;

  size_t offset = (text.data[0] == '+' || text.data[0] == '-') ? 1 : 0;
  if (offset == text.length) return false;

  return (
    isdigit((unsigned char)text.data[offset]) ||
    (
      (text.length - offset) >= 2 &&
      text.data[offset] == '.' &&
      isdigit((unsigned char)text.data[offset + 1])
    )
  );
}

// Parse numeric `Value` from a `String`.
//
// `TYPE_VOID` means infer the type from the literal's syntax.
// Otherwise parse directly into the requested numeric type.
//
// It saves the parsed result into the `result` `Value` pointer.
//
// Examples:
//
// - `1.` with target type `TYPE_VOID` → `TYPE_F32(1.0)`
// - `.123` with target type `TYPE_VOID` → `TYPE_F32(0.123)`
// - `1.e3` with target type `TYPE_VOID` → `TYPE_F32(1000.0)`
// - `123` with target type `TYPE_I32` → `TYPE_I32(123)`
// - `123` with target type `TYPE_F32` → `TYPE_F32(123.0)`
// - `4294967295` with target type `TYPE_U32` → `NUMBER_PARSE_OK`
// - `4294967296` with target type `TYPE_U32` → `NUMBER_PARSE_OUT_OF_RANGE`
// - `-2147483648` with target type `TYPE_I32` → `NUMBER_PARSE_OK`
// - `-2147483649` with target type `TYPE_I32` → `NUMBER_PARSE_OUT_OF_RANGE`
// - `-1` with target type `TYPE_U32` → `NUMBER_PARSE_INVALID`
// - `1.5` with target type `TYPE_I32` → `NUMBER_PARSE_INVALID`
// - `1e` with target type `TYPE_VOID` → `NUMBER_PARSE_INVALID_EXPONENT`
// - `1.e3` with target type `TYPE_I32` → `NUMBER_PARSE_INVALID`
// - `1\0x` (3 bytes text length) with target type `TYPE_VOID` → `NUMBER_PARSE_INVALID`
//
NumberParseResult parse_number_text(String text, ValueType target_type, Value *result) {
  if (text.length < 1 || text.data == NULL) return NUMBER_PARSE_INVALID;
  if (text.length >= NUMBER_TEXT_CAPACITY) return NUMBER_PARSE_TOO_LONG;

  size_t i = (text.data[0] == '+' || text.data[0] == '-') ? 1 : 0;
  if (text.length == i) return NUMBER_PARSE_INVALID;

  bool digits = false;
  bool floating = false;

  // Decimal digits
  while (text.length > i && isdigit((unsigned char)text.data[i])) { digits = true; i++; }
  if (digits && text.length == i) goto resolve;
  if (!digits && text.length == i) return NUMBER_PARSE_INVALID;

  // Detect floating point remainder.
  // Also supporting number literals such as `1.`, `-1.`, and `1.e3`
  //
  // Examples:
  //
  // - `1.` → Valid `f32`
  // - `-1.` → Valid `f32`
  // - `1.e3` → Valid `f32`
  // - `.123` → Valid `f32`
  // - `.` → Invalid
  // - `-.` → Invalid
  // - `1.e` → Invalid exponent
  //
  if (text.data[i] == '.') {
    floating = true;
    i++;
    while (text.length > i && isdigit((unsigned char)text.data[i])) { digits = true; i++; }
  }

  // If no digits detected
  if (!digits) return NUMBER_PARSE_INVALID; // Invalid numeric literal
  if (floating && text.length == i) goto resolve;

  // Support number exponent notation
  if (text.length > i && (text.data[i] == 'e' || text.data[i] == 'E')) {
    floating = true;
    i++;
    if (text.length == i) return NUMBER_PARSE_INVALID_EXPONENT; // Not enough exponent input
    if (text.data[i] == '+' || text.data[i] == '-') i++;
    if (text.length == i || !isdigit((unsigned char)text.data[i]))
      return NUMBER_PARSE_INVALID_EXPONENT; // Invalid numeric exponent
    while (text.length > i && isdigit((unsigned char)text.data[i])) i++;
  }

  resolve:
    // Not all input was consumed as a valid part of number
    if (text.length != i) return NUMBER_PARSE_INVALID; // Invalid numeric literal

    // Infer the type if it was not specified
    if (target_type == TYPE_VOID)
      target_type = floating ? TYPE_F32 : text.data[0] == '-' ? TYPE_I32 : TYPE_U32;

    // Exclude invalid cases before they reach a C parser
    if (
      // Prevent floating numbers to be parsed as integers
      ((target_type == TYPE_U32 || target_type == TYPE_I32) && floating) ||
      // Prevent `strtoull` to parse `-1` as `ULLONG_MAX`
      (target_type == TYPE_U32 && text.data[0] == '-')
    )
      return NUMBER_PARSE_INVALID;

    // For `strtof`, `strtoll`, and `strtoull` the string must be terminated.
    // So we need a temporary buffer that would include the terminator.
    char buffer[NUMBER_TEXT_CAPACITY] = {'\0'};
    memcpy(buffer, text.data, sizeof(char) * text.length);
    buffer[text.length] = '\0'; // Terminate the string

    errno = 0; // Reset global error
    char *end; // Pointer to a character where the parser stopped while not being able to parse it

    switch (target_type) {
      // Parse floating point fractional number
      case TYPE_F32: {
        // strtof(), accepting integer or floating-point syntax.
        float number = strtof(buffer, &end);
        NumberParseResult status =
          errno == ERANGE ? NUMBER_PARSE_OUT_OF_RANGE : // Float literal out of range
          end != buffer + text.length ? NUMBER_PARSE_INVALID : // C parser could not parse
          NUMBER_PARSE_OK;
        wipe_memory(buffer, sizeof(buffer)); // Do not wipe before checking `end`
        end = NULL;
        if (status != NUMBER_PARSE_OK) return status;
        result->type = TYPE_F32;
        result->as.f32 = number;
        return status;
      }

      // Parse integer (negative numbers are automatically detected as `i32` type)
      case TYPE_I32: {
        // Reject floating-point syntax.
        // strtoll(), check INT32_MIN..INT32_MAX.
        // Reserving large enough number type to detect out-of-bound
        long long number = strtoll(buffer, &end, 10);
        NumberParseResult status =
          errno == ERANGE ? NUMBER_PARSE_OUT_OF_RANGE : // i32 literal out of range
          end != buffer + text.length ? NUMBER_PARSE_INVALID : // C parser could not parse
          // i32 literal out of range
          (number < INT32_MIN || number > INT32_MAX) ? NUMBER_PARSE_OUT_OF_RANGE :
          NUMBER_PARSE_OK;
        wipe_memory(buffer, sizeof(buffer)); // Do not wipe before checking `end`
        end = NULL;
        if (status != NUMBER_PARSE_OK) return status;
        result->type = TYPE_I32;
        result->as.i32 = (int32_t)number;
        return status;
      }

      // Parse unsigned integer (positive numbers are automatically detected as `u32` type)
      case TYPE_U32: {
        // Reject floating-point syntax and negative sign.
        // strtoull(), check UINT32_MAX.
        // Reserving large enough number type to detect out-of-bound
        unsigned long long number = strtoull(buffer, &end, 10);
        NumberParseResult status =
          errno == ERANGE ? NUMBER_PARSE_OUT_OF_RANGE : // u32 literal out of range
          end != buffer + text.length ? NUMBER_PARSE_INVALID : // C parser could not parse
          number > UINT32_MAX ? NUMBER_PARSE_OUT_OF_RANGE : // u32 literal out of range
          NUMBER_PARSE_OK;
        wipe_memory(buffer, sizeof(buffer)); // Do not wipe before checking `end`
        end = NULL;
        if (status != NUMBER_PARSE_OK) return status;
        result->type = TYPE_U32;
        result->as.u32 = (uint32_t)number;
        return status;
      }

      default:
        wipe_memory(buffer, sizeof(buffer));
        return NUMBER_PARSE_INVALID;
    }
}
