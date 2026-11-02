#include <math.h>

#include "stdlib-pow-n-log.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "wipe-memory.h"
#include "evaluate.h"

// (pow) — Raise a numeric value to a power, preserving its type
Datum std_pow(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "pow";

  if (!validate_two_same_type_variables_inputs(fn_name, &fail_msg, args, argc))
    return return_value;

  Value value = { .type = args[0].as.value.type };

  switch (value.type) {
    // For u32 using `uint64_t` intermediate to prevent multiplication overflows
    case TYPE_U32: {
      uint64_t base = args[0].as.value.as.u32;
      uint32_t exponent = args[1].as.value.as.u32;
      uint64_t result = 1;

      // Exponentiation by squaring, checking for u32 overflow
      while (exponent != 0) {
        // Check if the exponent is odd (its last binary bit is 1)
        if (exponent & 1u) {
          // An odd exponent has one extra factor of `base` that cannot be
          // handled by squaring, so multiply it into the accumulated result.
          result *= base;
          if (result > UINT32_MAX) goto out_of_range;
        }

        // Divide the exponent by 2, discarding any remainder.
        // We can do this because the next step squares the base, so
        // raising the squared base to half the exponent gives the same power.
        exponent >>= 1;

        // Only square the base if more multiplications are needed.
        // Otherwise, we would do unnecessary work and could report overflow
        // even when the final result fits into u32.
        if (exponent != 0) {
          // Squaring the base compensates for halving the exponent.
          // For example, 2^10 = 4^5 = 16^2 * 4.
          base *= base;
          if (base > UINT32_MAX) goto out_of_range;
        }
      }

      value.as.u32 = (uint32_t)result;
      break;
    }

    // For i32 using `int64_t` intermediate to prevent multiplication overflows
    case TYPE_I32: {
      int64_t base = args[0].as.value.as.i32;
      int32_t signed_exponent = args[1].as.value.as.i32;

      if (signed_exponent < 0) goto negative_exponent;

      uint32_t exponent = (uint32_t)signed_exponent;
      int64_t result = 1;

      // Exponentiation by squaring, checking for i32 overflow
      while (exponent != 0) {
        // Check if the exponent is odd (its last binary bit is 1)
        if (exponent & 1u) {
          // An odd exponent has one extra factor of `base` that cannot be
          // handled by squaring, so multiply it into the accumulated result.
          result *= base;
          if (result < INT32_MIN || result > INT32_MAX) goto out_of_range;
        }

        // Divide the exponent by 2, discarding any remainder.
        // We can do this because the next step squares the base, so
        // raising the squared base to half the exponent gives the same power.
        exponent >>= 1;

        // Only square the base if more multiplications are needed.
        // Otherwise, we would do unnecessary work and could report overflow
        // even when the final result fits into i32.
        if (exponent != 0) {
          // Squaring the base compensates for halving the exponent.
          // For example, 2^10 = 4^5 = 16^2 * 4.
          base *= base;
          if (base < INT32_MIN || base > INT32_MAX) goto out_of_range;
        }
      }

      value.as.i32 = (int32_t)result;
      break;
    }

    case TYPE_F32: {
      float base = args[0].as.value.as.f32;
      float exponent = args[1].as.value.as.f32;

      if (!isfinite(base) || !isfinite(exponent)) goto invalid_value;

      value.as.f32 = powf(base, exponent);
      if (!isfinite(value.as.f32)) goto invalid_value;
      break;
    }

    default:
      report_unexpected_value_type(fn_name, &fail_msg, value.type);
      return return_value;
  }

  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = value };
  return return_value;

  negative_exponent:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) integer exponent must be non-negative", fn_name);
    goto fail;

  out_of_range:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) result exceeds the numeric type range", fn_name);
    goto fail;

  invalid_value:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid floating-point input or result", fn_name);
    goto fail;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (sqrt) — Square root
Datum std_sqrt(const Datum *args, size_t argc) {
  return std_unary_f32_math(args, argc, "sqrt", sqrtf);
}

// (log) — Natural logarithm
Datum std_log(const Datum *args, size_t argc) {
  return std_unary_f32_math(args, argc, "log", logf);
}

// (log10) — Base-10 logarithm
Datum std_log10(const Datum *args, size_t argc) {
  return std_unary_f32_math(args, argc, "log10", log10f);
}

// (exp) — Exponential function
Datum std_exp(const Datum *args, size_t argc) {
  return std_unary_f32_math(args, argc, "exp", expf);
}
