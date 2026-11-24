#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/c-string.h"

int main(void) {
  CString result = {0};
  const char source[] = "alphabet";

  // Complete strings and suffix views with a known terminator are borrowed.
  String complete = {
    .data = source,
    .base = source,
    .length = 8,
    .nul_terminated = true,
  };
  assert(string_to_c_string(complete, &result));
  assert(result.data == source && !result.owned && result.length == 8);
  clean_c_string(&result);
  assert(result.data == NULL && !result.owned);
  assert(source[0] == 'a');

  String suffix = {
    .data = source + 3,
    .base = source,
    .length = 5,
    .immutable_ref = true,
    .nul_terminated = true,
  };
  assert(string_to_c_string(suffix, &result));
  assert(result.data == source + 3 && !result.owned);
  assert(strcmp(result.data, "habet") == 0);
  clean_c_string(&result);

  // An interior slice needs its own terminator and must not modify the base.
  String middle = {
    .data = source + 2,
    .base = source,
    .length = 3,
    .immutable_ref = true,
  };
  assert(string_to_c_string(middle, &result));
  assert(result.owned && result.data != middle.data && result.length == 3);
  assert(strcmp(result.data, "pha") == 0);
  assert(source[5] == 'b');
  clean_c_string(&result);
  clean_c_string(&result); // Repeated cleanup is harmless.

  // A slice of zero bytes still needs a host terminator if not terminated.
  middle.length = 0;
  assert(string_to_c_string(middle, &result));
  assert(result.owned && result.data[0] == '\0');
  clean_c_string(&result);

  // An unallocated empty Lispico string can borrow the empty C string.
  assert(string_to_c_string((String) {0}, &result));
  assert(!result.owned && result.length == 0);
  assert(strcmp(result.data, "") == 0);
  clean_c_string(&result);

  // Conversion preserves embedded NUL bytes. Call sites must validate names.
  const char binary[] = { 'a', '\0', 'b', 'c' };
  String binary_view = { .data = binary, .base = binary, .length = 4 };
  assert(string_to_c_string(binary_view, &result));
  assert(result.owned && result.length == 4);
  assert(memcmp(result.data, binary, 4) == 0);
  assert(result.data[4] == '\0');
  clean_c_string(&result);

  // Invalid descriptors and arithmetic overflow are rejected without a copy.
  assert(!string_to_c_string((String) { .length = 1 }, &result));
  assert(!string_to_c_string((String) { .data = source, .length = SIZE_MAX }, &result));
  assert(result.data == NULL && !result.owned);
  assert(!string_to_c_string(complete, NULL));
  clean_c_string(NULL);

  puts("Host C-string conversion tests passed");
  return 0;
}
