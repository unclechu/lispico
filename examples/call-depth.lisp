; Top-level and ordinary (do) scopes are not function calls.
(assert (== (call-depth) 0))
(do
  (assert (== (call-depth) 0))
  (do
    (assert (== (call-depth) 0))))

; Nested calls increment the depth, which is restored on return.
(fn "inner-call-depth" (do
  (assert (== (call-depth) 2))
  (do
    (assert (== (call-depth) 2)))
  (call-depth)))
(fn "outer-call-depth" (do
  (assert (== (call-depth) 1))
  (assert (== (inner-call-depth) 2))
  (assert (== (call-depth) 1))
  (call-depth)))
(assert (== (outer-call-depth) 1))
(assert (== (call-depth) 0))

; Recursion uses the same nesting counter as ordinary nested calls.
(fn "recursive-call-depth" (args "n") (do
  (assert (== (call-depth) (- 4 n)))
  (if (== n 0)
    (call-depth)
    (recursive-call-depth (- n 1)))))
(assert (== (recursive-call-depth 3) 4))
(assert (== (call-depth) 0))

; Leaving a function also restores the caller’s call depth.
(fn "leave-call-depth" (leave 1 (call-depth)))
(assert (== (leave-call-depth) 1))
(assert (== (call-depth) 0))

; Dynamic function calls also increase the nesting depth.
(fn "dynamic-call-depth" (call-depth))
(fn "invoke-dynamic" (call "dynamic-call-depth"))
(assert (== (invoke-dynamic) 2))
(assert (== (call-depth) 0))
(assert (== (call "call-depth") 0))
