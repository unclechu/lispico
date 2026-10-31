; Sleep functions preserve the last evaluated result
123
(assert (== (sleep-s 0) 123))

456
(assert (== (sleep-ms 0.5) 456))

789
(assert (== (sleep-us 10.5) 789))

; Accept different numeric types
(sleep-s (type i32 0))
(sleep-ms 0.)
(sleep-us 0)

; Sleep for 10ms so it shows up in the time measurement table
(sleep-ms 10)
