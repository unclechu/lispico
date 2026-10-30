(println (type s 123)) ; Will print “123” as text
(println (type s -123)) ; Will print “-123” as text
(println (type s 3.5)) ; Will print “3.5” as text

(println (type s (type u32 "4294967295"))) ; Will print “4294967295” as text
(println (type s (type i32 "-2147483648"))) ; Will print “-2147483648” as text
(println (type s (type f32 ".5"))) ; Will print “0.5” as text

(== (type s "hello") "hello") ; SYM_VARIABLE(TYPE_U32(1))
