#pragma once

#include "evaluate.h"

// Some helpers for copying values into evaluator GC-owned runtime allocated data.

// Preserve immutable string views without allocation; otherwise copy into
// independently writable evaluator-owned runtime storage.
bool copy_runtime_string(Interpreter *interpreter, String source, String *destination);

// Preserve immutable array views without allocating. Mutable arrays get new
// descriptor/data storage; their immutable string elements remain borrowed,
// while mutable string elements receive independent copies.
bool copy_runtime_array(Interpreter *interpreter, Array source, Array *destination);
