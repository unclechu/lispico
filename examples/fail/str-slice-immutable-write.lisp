(var "part" (str-slice "Hello" 1 3))
(str-set "part" 0 (str-char "X")) ; Cannot write through a read-only view
