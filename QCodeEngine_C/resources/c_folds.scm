; Function bodies, if/for/while/switch/do blocks
(compound_statement) @fold

; Struct, union, enum bodies
(struct_specifier
  body: (field_declaration_list) @fold)

(union_specifier
  body: (field_declaration_list) @fold)

(enum_specifier
  body: (enumerator_list) @fold)

; Block comments  /* ... */
(comment) @fold

; Preprocessor conditional blocks
(preproc_ifdef)  @fold
(preproc_if)     @fold
(preproc_elif)   @fold
