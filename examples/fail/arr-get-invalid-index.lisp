; arr-get-invalid-index.lisp
(arr-new "foo" u32 1) (arr-get "foo" -1) ; Index must be u32
