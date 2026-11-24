(env-var-has (str-take "BAD\0NAME-more" 8)) ; A slice must still reject embedded NUL bytes
