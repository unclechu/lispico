; "Second registered failure" becomes the evaluation failure
; while "First registered failure" is also executed.
(do
  (defer (assert false "First registered failure"))
  (defer (assert false "Second registered failure")))
