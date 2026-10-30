#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "utils/term-colors.h"
#include "utils/report.h"
#include "../../src/number.h"

// Tests for `is_number_start`
typedef struct {
  const char *name;
  String text;
  bool expected;
} NumberStartTest;

// Tests for `parse_number_text`
typedef struct {
  const char *name;
  String text;
  ValueType target_type;
  NumberParseResult expected_status;
  Value expected_value;
} NumberTest;

// Preserve the exact byte length, including embedded NUL bytes.
#define TEXT(s) { .data = (s), .length = sizeof(s) - 1 }

int main(void) {
  set_term_colors();

  // Tests for `is_number_start`
  const NumberStartTest start_tests[] = {
    { "empty", TEXT(""), false },
    { "plus only", TEXT("+"), false },
    { "minus only", TEXT("-"), false },
    { "dot only", TEXT("."), false },
    { "plus dot", TEXT("+."), false },
    { "minus dot", TEXT("-."), false },

    { "zero", TEXT("0"), true },
    { "integer", TEXT("123"), true },
    { "positive integer", TEXT("+123"), true },
    { "negative integer", TEXT("-123"), true },

    { "fraction", TEXT("1.5"), true },
    { "leading dot", TEXT(".123"), true },
    { "positive leading dot", TEXT("+.123"), true },
    { "negative leading dot", TEXT("-.123"), true },
    { "trailing dot", TEXT("1."), true },

    { "exponent", TEXT("1e3"), true },
    { "incomplete exponent", TEXT("1e"), true },
    { "trailing characters", TEXT("123abc"), true },
    { "embedded NUL", TEXT("1\0x"), true },

    { "name", TEXT("abc"), false },
    { "name with digit", TEXT("abc123"), false },
    { "leading space", TEXT(" 123"), false },
    { "leading NUL", TEXT("\0x"), false },
  };

  // Tests for `parse_number_text`
  const NumberTest tests[] = {
    { "1.", TEXT("1."), TYPE_VOID, NUMBER_PARSE_OK,
      { .type = TYPE_F32, .as.f32 = 1.0f } },

    { ".123", TEXT(".123"), TYPE_VOID, NUMBER_PARSE_OK,
      { .type = TYPE_F32, .as.f32 = 0.123f } },

    { "1.e3", TEXT("1.e3"), TYPE_VOID, NUMBER_PARSE_OK,
      { .type = TYPE_F32, .as.f32 = 1000.0f } },

    { "123 as i32", TEXT("123"), TYPE_I32, NUMBER_PARSE_OK,
      { .type = TYPE_I32, .as.i32 = 123 } },

    { "123 as f32", TEXT("123"), TYPE_F32, NUMBER_PARSE_OK,
      { .type = TYPE_F32, .as.f32 = 123.0f } },

    { "UINT32_MAX", TEXT("4294967295"), TYPE_U32, NUMBER_PARSE_OK,
      { .type = TYPE_U32, .as.u32 = UINT32_MAX } },

    { "u32 overflow", TEXT("4294967296"), TYPE_U32, NUMBER_PARSE_OUT_OF_RANGE, {0} },

    { "INT32_MIN", TEXT("-2147483648"), TYPE_I32, NUMBER_PARSE_OK,
      { .type = TYPE_I32, .as.i32 = INT32_MIN } },

    { "i32 underflow", TEXT("-2147483649"), TYPE_I32, NUMBER_PARSE_OUT_OF_RANGE, {0} },

    { "negative u32", TEXT("-1"), TYPE_U32, NUMBER_PARSE_INVALID, {0} },

    { "fractional i32", TEXT("1.5"), TYPE_I32, NUMBER_PARSE_INVALID, {0} },

    { "invalid exponent", TEXT("1e"), TYPE_VOID, NUMBER_PARSE_INVALID_EXPONENT, {0} },

    { "exponent as i32", TEXT("1.e3"), TYPE_I32, NUMBER_PARSE_INVALID, {0} },

    { "embedded NUL", TEXT("1\0x"), TYPE_VOID, NUMBER_PARSE_INVALID, {0} },
  };

  size_t passed = 0;
  size_t failed = 0;

  print_test_header("is_number_start()");
  for (size_t i = 0; i < sizeof(start_tests) / sizeof(start_tests[0]); ++i) {
    const NumberStartTest *test = &start_tests[i];

    bool actual_result = is_number_start(test->text);
    bool ok = actual_result == test->expected;

    report_test(
      test->name,
      ok,
      test->expected ? "true" : "false",
      actual_result ? "true" : "false",
      &passed,
      &failed
    );
  }

  print_test_header("parse_number_text()");
  for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
    const NumberTest *test = &tests[i];
    Value result = {0};

    NumberParseResult status = parse_number_text(test->text, test->target_type, &result);

    bool ok = status == test->expected_status;

    if (ok && status == NUMBER_PARSE_OK) {
      ok = result.type == test->expected_value.type;

      if (ok) {
        switch (result.type) {
          case TYPE_U32: ok = result.as.u32 == test->expected_value.as.u32; break;
          case TYPE_I32: ok = result.as.i32 == test->expected_value.as.i32; break;
          case TYPE_F32:
            ok = isfinite(result.as.f32) && result.as.f32 == test->expected_value.as.f32;
            break;
          default: ok = false; break;
        }
      }
    }

    char expected[128] = {'\0'};
    snprintf(expected, sizeof(expected), "%d", (int)test->expected_status);
    char actual[128] = {'\0'};
    snprintf(actual, sizeof(actual), "%d", (int)status);
    report_test(test->name, ok, expected, actual, &passed, &failed);
  }

  return summary_report("Number", passed, failed);
}
