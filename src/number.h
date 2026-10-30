#pragma once

#include "symbol.h"

typedef enum {
  NUMBER_PARSE_OK = 0,
  NUMBER_PARSE_INVALID,
  NUMBER_PARSE_INVALID_EXPONENT,
  NUMBER_PARSE_OUT_OF_RANGE,
  NUMBER_PARSE_TOO_LONG,
} NumberParseResult;

bool is_number_start(String text);

// Parse numeric `Value`.
//
// `TYPE_VOID` means infer the type from the literal's syntax.
// Otherwise parse directly into the requested numeric type.
NumberParseResult parse_number_text(String text, ValueType target_type, Value *result);
