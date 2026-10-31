; "Original failure" becomes the evaluation failure
; while "Deferred failure" is executed too.
(do
  (defer (assert false "Deferred failure"))
  (assert false "Original failure"))
