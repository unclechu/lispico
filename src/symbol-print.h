#pragma once

#include "symbol.h"

const char* show_SymbolKind(SymbolKind kind);
const char* show_ValueType(ValueType type);

// WARNING! Caller must free the allocated resulting string!
char* show_String(String string);

// WARNING! Caller must free the allocated resulting string!
char* show_Value(Value value);

// WARNING! Caller must free the allocated resulting string!
char* show_Array(Array array);
