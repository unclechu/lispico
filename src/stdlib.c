#include <float.h>

#include "stdlib.h"

// stdlib modules
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

#include "symbol-error.h"

// Populate the `symbols` with “standard library” definitions with
// implementations unspecific to a target platform.
bool populate_stdlib(Interpreter *interpreter, char (*error_message)[ERR_MSG_SIZE]) {
  const char *symbol_name;

  POPULATE_STDLIB();

  return true;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(interpreter, error_message, symbol_name);
    return false;
}
