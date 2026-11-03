; Reading regular files returns strings with their exact byte lengths
(assert (== (read-file "tests/fixtures/file-io-input.txt") "Hello from a file\nSecond line\n"))
(assert (== (str-size (read-file "tests/fixtures/file-io-input.txt")) 30))
(assert (== (type-of (read-file "tests/fixtures/file-io-input.txt")) s))

; Device files do not need to provide a regular file size
(assert (== (read-file "/dev/null") ""))
(assert (== (str-size (read-file "/dev/null")) 0))

; Empty contents and embedded NUL bytes are valid output
(write-file "/dev/null" "")
(write-file "/dev/null" "abc\0def")

; Writing to /dev/stdout uses the same chunked writer
(write-file "/dev/stdout" "Hello from write-file\n")

; An example of multiple chunks (4KiB) reading
(assert
  (> (str-size (read-file "README.md")) (* 1024 10))
  "README.md must be more than 10 KiB")
