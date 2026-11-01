// Select POSIX.1-2008 specification.
// Make `nanosleep()` available through `<time.h>` on POSIX-compatible systems.
#define _POSIX_C_SOURCE 200809L

#include <time.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include "stdlib-sleep.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "evaluate.h"
#include "wipe-memory.h"

// Sleep for a specified duration, with fractional values supported.
// Returns the last evaluated value without changing it.
static Datum std_sleep_generic(
  const Datum *args,
  size_t argc,
  const char *fn_name,
  double seconds_per_unit
) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  if (!validate_argc(fn_name, &fail_msg, 1, argc)) return return_value;

  if (args[0].kind != SYM_VARIABLE) goto invalid_duration;

  double duration;
  switch (args[0].as.value.type) {
    case TYPE_U32: duration = (double)args[0].as.value.as.u32; break;
    case TYPE_I32: duration = (double)args[0].as.value.as.i32; break;
    case TYPE_F32: duration = (double)args[0].as.value.as.f32; break;
    default: goto invalid_duration;
  }

  double seconds = duration * seconds_per_unit;

  if (!isfinite(seconds) || seconds < 0.0 || seconds > (double)UINT32_MAX)
    goto invalid_duration;

  // Separate whole seconds and fractional nanoseconds.
  uint64_t whole_seconds = (uint64_t)seconds;
  long nanoseconds = (long)round((seconds - (double)whole_seconds) * 1000.0 * 1000.0 * 1000.0);

  // If nanoseconds amount round to a whole second then count the fraction part as a whole second
  if (nanoseconds >= 1000L * 1000L * 1000L) {
    if (whole_seconds >= UINT32_MAX) goto invalid_duration;
    whole_seconds++;
    nanoseconds = 0;
  }

  // Split long sleeps so they also fit a 32-bit signed `time_t`.
  while (whole_seconds != 0 || nanoseconds != 0) {
    uint64_t chunk = whole_seconds > INT_MAX ? INT_MAX : whole_seconds;

    struct timespec request = {
      .tv_sec = (time_t)chunk,
      .tv_nsec = whole_seconds > INT_MAX ? 0 : nanoseconds,
    };

    struct timespec remaining;

    // Resume sleeping if interrupted by a system signal.
    while (nanosleep(&request, &remaining) != 0) {
      if (errno != EINTR) {
        int error_code = errno;
        snprintf(
          fail_msg,
          sizeof(fail_msg),
          "(%s) sleep failed: %s",
          fn_name,
          strerror(error_code)
        );
        goto fail;
      }
      request = remaining;
    }

    whole_seconds -= chunk;
    if (whole_seconds == 0) nanoseconds = 0;
  }

  // Preserve the last successfully evaluated value.
  // When there is no nonempty previous value, return `SYM_EMPTY`.
  // `get_last_evaluation_value` may fail on `SYM_EMPTY` and won’t update
  // `return_value` but default `return_value` is `SYM_EMPTY` anyway.
  // So that the value is forwarded correctly.
  (void)get_last_evaluation_value(&return_value);
  return return_value;

  invalid_duration:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) expects a non-negative, finite numeric duration (u32, i32, or f32)",
      fn_name
    );

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (sleep-s) — Sleep for a duration in seconds
Datum std_sleep_s(const Datum *args, size_t argc) {
  return std_sleep_generic(args, argc, "sleep-s", 1.0);
}

// (sleep-ms) — Sleep for a duration in milliseconds
Datum std_sleep_ms(const Datum *args, size_t argc) {
  return std_sleep_generic(args, argc, "sleep-ms", 0.001);
}

// (sleep-us) — Sleep for a duration in microseconds
Datum std_sleep_us(const Datum *args, size_t argc) {
  return std_sleep_generic(args, argc, "sleep-us", 0.000001);
}

// Populate the `symbols` with “standard library” sleeping functions
bool populate_stdlib_sleep(char (*error_message)[ERR_MSG_SIZE]) {
  const char *symbol_name;

  // Sleep functions
  ADD_FN_SYMBOL("sleep-s", std_sleep_s);
  ADD_FN_SYMBOL("sleep-ms", std_sleep_ms);
  ADD_FN_SYMBOL("sleep-us", std_sleep_us);

  return true;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(error_message, symbol_name);
    return false;
}
