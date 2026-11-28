// POSIX monotonic clock support for desktop applications.
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "stdlib-time.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "wipe-memory.h"

// (monotonic-ms) — Return the low 32 bits of monotonic milliseconds.
// This wraps every 2^32 milliseconds (approximately 49.7 days). Unsigned
// subtraction can measure shorter elapsed intervals across that wrap.
Datum std_monotonic_ms(Interpreter *interpreter, const Datum *args, size_t argc) {
  (void)args; // Ignore unused argument warning
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "monotonic-ms";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 0, argc)) return return_value;

  struct timespec timestamp;
  if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) {
    int error_code = errno;
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) failed to read monotonic clock: %s",
      fn_name,
      strerror(error_code)
    );
    goto fail;
  }

  // Convert the timestamp to whole milliseconds, truncating sub-millisecond
  // fractions. Casting to u32 keeps the low 32 bits, wrapping modulo 2^32.
  uint64_t milliseconds =
    ((uint64_t)timestamp.tv_sec * 1000u) + ((uint64_t)timestamp.tv_nsec / 1000000u);

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = (uint32_t)milliseconds },
  };
  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
