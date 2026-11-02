; Read ASCII bytes from string literals
(assert (== (str-char "hello" 0) 104))
(assert (== (str-char "hello" 1) 101))
(assert (== (str-char "hello" 2) 108))
(assert (== (str-char "hello" 3) 108))
(assert (== (str-char "hello" 4) 111))

; String indexes are zero-based
(assert (== (str-char "ABC" 0) 65))
(assert (== (str-char "ABC" 1) 66))
(assert (== (str-char "ABC" 2) 67))

; Spaces and punctuation are bytes too
(assert (== (str-char "a b!" 1) 32))
(assert (== (str-char "a b!" 3) 33))

; Read bytes from string variables
(var "message" "Lispico")
(assert (== (str-char message 0) 76))
(assert (== (str-char message 6) 111))

; Read bytes from evaluated string expressions
(assert (== (str-char (str "hello" " world") 5) 32))
(assert (== (str-char (type s 12345) 0) 49))
(assert (== (str-char (type s 12345) 4) 53))

; Index can be an evaluated expression
(assert (== (str-char "hello" (- (str-size "hello") 1)) 111))

; Results are always u32
(assert (== (type-of (str-char "hello" 0)) u32))

; UTF-8 characters may consist of multiple bytes
(assert (== (str-size "é") 2))
(assert (== (str-char "é" 0) 195))
(assert (== (str-char "é" 1) 169))
