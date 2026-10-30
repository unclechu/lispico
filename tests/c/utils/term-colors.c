#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *o_reset_color = "";
const char *o_secondary_color = "";
const char *o_pass_color = "";
const char *o_fail_color = "";

const char *e_reset_color = "";
const char *e_fail_color = "";

void set_term_colors() {
  const bool force_color =
    getenv("FORCE_COLOR") && strcmp(getenv("FORCE_COLOR"), "1") == 0;

  const bool github_actions =
    getenv("GITHUB_ACTIONS") && strcmp(getenv("GITHUB_ACTIONS"), "true") == 0;

  if (isatty(STDOUT_FILENO) || force_color || github_actions) {
    o_reset_color = "\033[0m";
    o_secondary_color = "\033[1m";
    o_pass_color = "\033[32m";
    o_fail_color = "\033[1;37;41m";
  }

  if (isatty(STDERR_FILENO) || force_color || github_actions) {
    e_reset_color = "\033[0m";
    e_fail_color = "\033[1;37;41m";
  }
}
