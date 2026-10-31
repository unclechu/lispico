(label "start")

; `(goto)` should not be evaluated because `(and)` and `(or)` are
; short-circuiting after the first argument. Otherwise the program would loop
; indefinitely.
(assert (== (and false (goto start)) false))
(assert (== (or true (goto start)) true))
