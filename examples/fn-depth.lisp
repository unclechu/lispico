; Outside user-defined functions, both helpers return zero even inside `(do)`.
(assert (== (fn-depth) 0))
(assert (== (fn-return-depth) 0))
(do
  (assert (== (depth) 1))
  (assert (== (fn-depth) 0))
  (assert (== (fn-return-depth) 0))
  (do
    (assert (== (fn-depth) 0))
    (assert (== (fn-return-depth) 0))))

; The implicit function scope exists even without an explicit body `(do)`.
(fn "absolute-implicit" (fn-depth))
(fn "return-implicit" (fn-return-depth))
(assert (== (absolute-implicit) 1))
(assert (== (return-implicit) 1))
(do
  (do
    (assert (== (absolute-implicit) 3))
    (assert (== (return-implicit) 1))))

; At top-level invocation, the absolute implicit depth is a valid return count.
(fn "leave-top-plain" (leave (fn-depth) 71))
(assert (== (leave-top-plain) 71))

; A direct call at top level has absolute implicit depth 1.
(fn "inspect-top" (do
  (assert (== (fn-depth) 1))
  (assert (== (depth) 2))
  (assert (== (fn-return-depth) 2))
  (do
    (assert (== (fn-depth) 1))
    (assert (== (depth) 3))
    (assert (== (fn-return-depth) 3)))
  (assert (== (fn-return-depth) 2))
  42))
(assert (== (inspect-top) 42))

; At the function’s implicit top level, `(fn-depth)` can be used as a
; `(leave)` count only if the call was made at program depth zero.
(fn "leave-top" (do
  (assert (== (fn-depth) 1))
  (assert (== (fn-return-depth) 2))
  (leave (fn-return-depth) 42)))
(assert (== (leave-top) 42))

; An explicit body `(do)` is another scope. From the innermost block,
; `(leave (fn-depth))` leaves just one scope for a top-level invocation.
(var "after-inner" false)
(fn "leave-inner-only" (do
  (assert (== (fn-depth) 1))
  (assert (== (fn-return-depth) 2))
  (do
    (assert (== (fn-depth) 1))
    (assert (== (fn-return-depth) 3))
    (leave (fn-depth) 0)
    (assert false))
  (set "after-inner" true)
  99))
(assert (== (leave-inner-only) 99))
(assert after-inner)

; A dynamic return count includes all `(do)` scopes within this function.
; Deferred handlers still run when those scopes are exited.
(var "defer-count" 0)
(fn "leave-from-inner" (do
  (defer (set "defer-count" (+ defer-count 1)))
  (do
    (defer (set "defer-count" (+ defer-count 1)))
    (do
      (assert (== (fn-depth) 1))
      (assert (== (fn-return-depth) 4))
      (leave (fn-return-depth) 123)
      (assert false)))
  (assert false)))
(assert (== (leave-from-inner) 123))
(assert (== defer-count 2))

; The absolute depth includes the caller’s `(do)` scopes, while the
; relative return count only includes scopes inside the current call.
(fn "callee" (do
  (assert (== (fn-depth) 4))
  (assert (== (depth) 5))
  (assert (== (fn-return-depth) 2))
  (leave (fn-return-depth) 17)))
(fn "caller" (do
  (assert (== (fn-depth) 2))
  (assert (== (depth) 3))
  (assert (== (fn-return-depth) 2))
  (assert (== (callee) 17))
  (assert (== (fn-depth) 2))
  (assert (== (fn-return-depth) 2))
  (leave (fn-return-depth) 18)))
(do
  (assert (== (caller) 18))
  (assert (== (fn-depth) 0))
  (assert (== (fn-return-depth) 0)))

; An absolute `(fn-depth)` count can unwind caller scopes as well.
(var "skipped" false)
(fn "leave-to-program" (leave (fn-depth) 77))
(do
  (do
    (leave-to-program)
    (set "skipped" true))
  (set "skipped" true))
(assert (== skipped false))

; Reentrant calls each have their own absolute implicit scope depth.
(fn "recursive" (args "n") (do
  (assert (== (fn-depth) (- 7 (* 2 n))))
  (assert (== (fn-return-depth) 2))
  (if (== n 0)
    (do
      (assert (== (fn-return-depth) 3))
      (leave (fn-return-depth) 321)))
  (recursive (- n 1))))
(assert (== (recursive 3) 321))
(assert (== (fn-depth) 0))
(assert (== (fn-return-depth) 0))
