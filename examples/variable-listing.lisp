; Both functions return exact stored names in sorted string arrays.
; At the top level, the visible symbols are the global symbols.
(var "user-global" 123)
(var "void-global" 42)
(reset "void-global")
(gc)
(assert (== (local-variables) (variables)))
(gc)
(assert (== (arr-type (variables)) s))
(assert (== (is-immutable-ref (variables)) false))
(assert (has-immutable-refs (variables)))
(assert (is-immutable-ref (arr-get (variables) 0)))
(var "names-for-editing" (variables))
(arr-set "names-for-editing" 0 (copy (arr-get names-for-editing 0)))
(assert (== (is-immutable-ref (arr-get names-for-editing 0)) false))
(assert (has-immutable-refs names-for-editing))

; Inspect the resulting arrays without copying them into named arguments.
(fn "has-name" (var-args) (do
  (gc)
  (local "index" 0)
  (local "found" false)
  (local-label "loop")
  (if (< index (arr-size (get-arg 0))) (do
    (if (== (arr-get (get-arg 0) index) (get-arg 1))
      (set "found" true))
    (set "index" (+ index 1))
    (goto loop)))
  found))

; Check a generated local name without relying on a fixed expression index.
(fn "has-local-ending" (var-args) (do
  (gc)
  (local "index" 0)
  (local "found" false)
  (local-label "loop")
  (if (< index (arr-size (get-arg 0))) (do
    (local "name" (arr-get (get-arg 0) index))
    (if (>= (str-size name) 7)
      (if (== (str-take name 7) "_LOCAL_")
        (if (>= (str-size name) (str-size (get-arg 1)))
          (if (==
            (str-drop name (- (str-size name) (str-size (get-arg 1))))
            (get-arg 1)
          ) (set "found" true)))))
    (set "index" (+ index 1))
    (goto loop)))
  found))

; All occupied symbols are included, even native functions and labels.
(label "global-jump")
(gc)
(assert (has-name (variables) "user-global"))
(gc)
(assert (has-name (variables) "void-global"))
(gc)
(assert (has-name (variables) "true"))
(gc)
(assert (has-name (variables) "variables"))
(gc)
(assert (has-name (variables) "global-jump"))
(gc)
(assert (has-name (variables) "_FN_has-name"))

(var "shadowed" "global")
(do
  (local "shadowed" "local")
  (local "only-local" 7)
  (local-label "global-jump")

  ; Raw enumeration includes global and local entries, including the label.
  (gc)
  (assert (has-name (variables) "shadowed"))
  (gc)
  (assert (has-local-ending (variables) "_shadowed"))
  (gc)
  (assert (has-local-ending (variables) "_only-local"))
  (gc)
  (assert (has-local-ending (variables) "_global-jump"))

  ; Visible enumeration hides the masked globals without renaming locals.
  (gc)
  (assert (!= (has-name (local-variables) "shadowed") true))
  (gc)
  (assert (has-local-ending (local-variables) "_shadowed"))
  (gc)
  (assert (has-local-ending (local-variables) "_only-local"))
  (gc)
  (assert (!= (has-name (local-variables) "global-jump") true))
  (gc)
  (assert (has-local-ending (local-variables) "_global-jump"))
  (gc)
  (assert (has-name (local-variables) "user-global"))
  (assert (== (get "shadowed") "local"))

  (do
    (local "shadowed" 99)
    (gc)
    (assert (!= (has-name (local-variables) "shadowed") true))
    (gc)
    (assert (has-local-ending (local-variables) "_shadowed"))
    (gc)
    (assert (has-local-ending (local-variables) "_only-local"))
    (assert (== (get "shadowed") 99)))
  (assert (== shadowed "local")))

; The local symbols remain in the table after their scope exits.
(assert (== shadowed "global"))
(gc)
(assert (!= (has-local-ending (local-variables) "_only-local") true))
(gc)
(assert (has-local-ending (variables) "_only-local"))
(gc)
(assert (has-local-ending (variables) "_shadowed"))

; A global name resembling an internal local name stays global.
(var "_LOCAL_999999_manual" 123)
(gc)
(assert (has-name (variables) "_LOCAL_999999_manual"))
(gc)
(assert (has-name (local-variables) "_LOCAL_999999_manual"))

; Function-local arguments and `argc` are visible under their actual names.
(fn "inspect" (args "input") (do
  (gc)
  (assert (has-local-ending (local-variables) "_input"))
  (gc)
  (assert (has-local-ending (local-variables) "_argc"))
  (gc)
  (assert (has-name (local-variables) "user-global"))
  (gc)
  (assert (!= (has-name (local-variables) "input") true))
  (gc)
  (assert (has-local-ending (variables) "_input"))
  input))
(assert (== (inspect 42) 42))

; A returned name array has independent storage and survives a manual GC.
(gc)
(var "snapshot" (variables))
(reset "user-global")
(gc)
(assert (has-name snapshot "user-global"))
