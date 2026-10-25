#include "stdlib-print.h"

// Get a string representation of `show_StdPrintVariant` value
const char* show_StdPrintVariant(StdPrintVariant variant) {
  switch (variant) {
    case STD_PRINT: return "print";
    case STD_PRINTLN: return "println";
    case STD_EPRINT: return "eprint";
    case STD_EPRINTLN: return "eprintln";
    default: return "( ! Unexpected StdPrintVariant for show_StdPrintVariant ! )";
  }
}
