; Variable definitions are global and types do not change.
(var "counter" u32 5)
(set "counter" (+ counter 3))
counter ; TYPE_U32(8)
