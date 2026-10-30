#include "report.h"
#include "term-colors.h"

void report_test(
  const char *test_name,
  bool ok,
  const char *expected,
  const char *actual,
  size_t *passed,
  size_t *failed
) {
  if (ok) {
    printf("%s[PASS]%s %s\n", o_pass_color, o_reset_color, test_name);
    (*passed)++;
  } else {
    fprintf(
      stderr,
      "%s[FAIL]%s %s: expected: “%s”, got “%s”\n",
      e_fail_color,
      e_reset_color,
      test_name,
      expected,
      actual
    );
    (*failed)++;
  }
}

int summary_report(char *suit_name, size_t passed, size_t failed) {
  const char *pass_hl = failed == 0 ? o_pass_color : o_secondary_color;
  const char *fail_hl = failed != 0 ? o_fail_color : o_secondary_color;

  printf("••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••\n");

  printf(
    "%s tests: %sPassed: %zu%s, %sFailed: %zu%s\n",
    suit_name,
    pass_hl, passed, o_reset_color,
    fail_hl, failed, o_reset_color
  );

  return failed == 0 && passed > 0 ? 0 : 1;
}
