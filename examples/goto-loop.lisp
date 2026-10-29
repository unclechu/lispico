; Increment a counter until it reaches 5.
(var "counter" 0)

(label "loop")

(set "counter" (+ counter 1))

(if (< counter 5)
  (goto loop)
  counter)

counter ; Result: SYM_VARIABLE(TYPE_U32(5))
