; Verify that bounded reading from stdin leaves the remaining stream available
(assert (== (read-file "/dev/stdin" 0) ""))
(assert (== read-file-status read-file-status-truncated))
(var "prefix" (read-file "/dev/stdin" 4096))
(assert (== (str-size prefix) 4096))
(assert (== read-file-status read-file-status-truncated))
(write-file (arr-get argv 2) prefix)
(write-file (arr-get argv 3) (read-file "/dev/stdin"))
; Unlimited reads must not update the status
(assert (== read-file-status read-file-status-truncated))
