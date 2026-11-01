; Variable definitions are global and types do not change.
(var "counter" 5)
(set "counter" (+ counter 3))
(assert (== counter 8) "Final counter value must be 8")
counter ; TYPE_U32(8)
