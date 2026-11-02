#include "stdlib.h"
#include "stdlib-arithmetics.h"
#include "stdlib-numeric-fns.h"
#include "stdlib-pow-n-log.h"
#include "stdlib-trigonometry.h"
#include "stdlib-comparisons.h"
#include "stdlib-strings.h"
#include "stdlib-variables.h"
#include "stdlib-types.h"
#include "stdlib-jumps.h"
#include "stdlib-sequences.h"
#include "stdlib-eval-control.h"
#include "stdlib-assertions.h"

// Populate the `symbols` with “standard library” definitions with
// implementations unspecific to a target platform.
bool populate_stdlib(char (*error_message)[ERR_MSG_SIZE]) {
  // Assertions
  if (!populate_stdlib_assertions(error_message)) return false;

  // Evaluation control
  if (!populate_stdlib_eval_control(error_message)) return false;

  // Sequences
  if (!populate_stdlib_sequences(error_message)) return false;

  // Variables
  if (!populate_stdlib_variables(error_message)) return false;

  // Types
  if (!populate_stdlib_types(error_message)) return false;

  // Strings
  if (!populate_stdlib_strings(error_message)) return false;

  // Arithmetics
  if (!populate_stdlib_arithmetics(error_message)) return false;
  // Basic numeric functions
  if (!populate_stdlib_numeric_fns(error_message)) return false;
  // Powers and logarithms
  if (!populate_stdlib_pow_n_log(error_message)) return false;
  // Trigonometry
  if (!populate_stdlib_trigonometry(error_message)) return false;

  // Comparisons
  if (!populate_stdlib_comparisons(error_message)) return false;

  // Jumps
  if (!populate_stdlib_jumps(error_message)) return false;

  return true;
}
