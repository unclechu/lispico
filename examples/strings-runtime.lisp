; Strings concatenation using `(str)` (runtime string allocation)

(var "message" "Hello")
(set "message" (str message ", " (str "Lisp" "ico")))
(println message) ; Will print “Hello, Lispico”

(println (str)) ; Will print just an empty line

; Will print “a<NUL>bc” (`(println)` includes `\0` terminators).
; Depending on terminal it will likely look like just “abc”.
(println (str "a\0b" "c"))

(== message "Hello, Lispico") ; Result: SYM_VARIABLE(TYPE_U32(1))
