; Copy the entire string
(assert (== (str-copy "Hello") "Hello"))
(assert (== (str-copy "") ""))
(assert (== (str-size (str-copy "Hello")) 5))
(assert (== (type-of (str-copy "Hello")) s))

; Copy only the requested number of bytes
(assert (== (str-copy "Hello" 1) "H"))
(assert (== (str-copy "Hello" 3) "Hel"))
(assert (== (str-copy "Hello" 5) "Hello"))
(assert (== (str-copy "Hello" 0) ""))
(assert (== (str-size (str-copy "Hello" 3)) 3))
(assert (== (str-size (str-copy "Hello" 0)) 0))

; Embedded NUL bytes are copied, not treated as terminators
(assert (== (str-copy "abc\0def") "abc\0def"))
(assert (== (str-copy "abc\0def" 3) "abc"))
(assert (== (str-copy "abc\0def" 4) "abc\0"))
(assert (== (str-copy "abc\0def" 5) "abc\0d"))
(assert (== (str-size (str-copy "abc\0def" 5)) 5))
(assert (== (str-char (str-copy "abc\0def" 5) 3) 0))

; Copy from evaluated expressions
(assert (== (str-copy (str "Hello" " world") 5) "Hello"))
(assert (== (str-copy (type s 12345) 3) "123"))

; Copy from a string variable
(var "message" "Hello world")
(assert (== (str-copy message 5) "Hello"))
(assert (== (str-copy message) "Hello world"))

; The copy preserves the original bytes after the source changes
(var "copied" (str-copy message 5))
(str-set "message" "Hi")
(assert (== copied "Hello"))
(assert (== (str-size copied) 5))
(assert (== (str-size message) 11))

; Create a shorter copy after zero-filling the original string
(assert (== (str-copy message 2) "Hi"))
(assert (== (str-size (str-copy message 2)) 2))

; Copy only the meaningful bytes before the first NUL
(var "longer" "Hello world")
(str-set "longer" "Hi")
(var "shorter" (str-copy longer (str-len longer)))
(assert (== (str-size longer) 11))
(assert (== (str-size shorter) 2))
(assert (== shorter "Hi"))
