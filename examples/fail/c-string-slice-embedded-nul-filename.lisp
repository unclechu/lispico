(file-exists (str-slice "abc\0DEF" 1 4)) ; A slice must still reject embedded NUL bytes
