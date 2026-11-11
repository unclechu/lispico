#pragma once

#include "evaluate.h"

// Some helpers for copying values into evaluator GC-owned runtime allocated data.

// Copy a `String` from process arguments into evaluator-owned runtime data.
bool copy_runtime_string(Interpreter *interpreter, String source, String *destination);

// Copy an `Array` into runtime-managed storage.
//
// Zero-initialized string elements are left empty.
// Supplied strings each get their own copy.
bool copy_runtime_array(Interpreter *interpreter, Array source, Array *destination);
