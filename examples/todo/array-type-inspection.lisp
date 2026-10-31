; TODO: Once arrays are implemented move outside of `todo/`

(arr-new "numbers" u32 16)
(assert (== (kind-of "numbers") arr-kind))
(assert (== (arr-type "numbers") u32))
