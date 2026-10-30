#pragma once

#include <stdio.h>
#include <stdbool.h>

void report_test(
  const char *test_name,
  bool ok,
  const char *expected,
  const char *actual,
  size_t *passed,
  size_t *failed
);

int summary_report(char *suit_name, size_t passed, size_t failed);
