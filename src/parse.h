#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "expression.h"

// Parse source into the global `expressions` and `expression_allocations` tables.
// Clears previous expressions first; leaves both tables empty on failure.
bool parse_source(const char *source, size_t length, char (*error)[ERR_MSG_SIZE]);
