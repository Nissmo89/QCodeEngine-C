#pragma once

// =============================================================================
// Tree-sitter SCM Queries for C — sourced from Zed editor (battle-tested)
// Source: https://github.com/zed-industries/zed/tree/main/crates/grammars/src/c
// =============================================================================

// ---------------------------------------------------------------------------
// highlights.scm — Syntax highlighting captures
// ---------------------------------------------------------------------------
constexpr const char* HIGHLIGHTS_SCM = R"SCM(
[
  "const"
  "enum"
  "extern"
  "inline"
  "sizeof"
  "static"
  "struct"
  "typedef"
  "union"
  "volatile"
] @keyword

[
  "break"
  "case"
  "continue"
  "default"
  "do"
  "else"
  "for"
  "goto"
  "if"
  "return"
  "switch"
  "while"
] @keyword.control

[
  "#define"
  "#elif"
  "#elifdef"
  "#elifndef"
  "#else"
  "#endif"
  "#if"
  "#ifdef"
  "#ifndef"
  "#include"
  (preproc_directive)
] @keyword.preproc @preproc

[
  "="
  "+="
  "-="
  "*="
  "/="
  "%="
  "&="
  "|="
  "^="
  "<<="
  ">>="
  "++"
  "--"
  "+"
  "-"
  "*"
  "/"
  "%"
  "~"
  "&"
  "|"
  "^"
  "<<"
  ">>"
  "!"
  "&&"
  "||"
  "=="
  "!="
  "<"
  ">"
  "<="
  ">="
  "->"
  "?"
  ":"
] @operator

[
  "."
  ";"
  ","
] @punctuation.delimiter

[
  "{"
  "}"
  "("
  ")"
  "["
  "]"
] @punctuation.bracket

[
  (string_literal)
  (system_lib_string)
  (char_literal)
] @string

(escape_sequence) @string.escape

(preproc_arg) @preproc.arg

(comment) @comment

(number_literal) @number

[
  (true)
  (false)
] @boolean

(null) @constant.builtin

(identifier) @variable

((identifier) @constant
  (#match? @constant "^_*[A-Z][A-Z\\d_]*$"))

(call_expression
  function: (identifier) @function)

(call_expression
  function: (field_expression
    field: (field_identifier) @function))

(function_declarator
  declarator: (identifier) @function)

(preproc_function_def
  name: (identifier) @function.special)

(field_identifier) @property

(statement_identifier) @label

[
  (type_identifier)
  (primitive_type)
  (sized_type_specifier)
] @type

; GNU __attribute__
(attribute_specifier) @attribute

(attribute_specifier
  (argument_list
    (identifier) @attribute))

; C23 [[attributes]]
(attribute
  prefix: (identifier) @attribute)

(attribute
  name: (identifier) @attribute)
)SCM";

// ---------------------------------------------------------------------------
// brackets.scm — Bracket pair definitions (used for rainbow brackets / matching)
// ---------------------------------------------------------------------------
constexpr const char* BRACKETS_SCM = R"SCM(
("(" @open
  ")" @close)

("[" @open
  "]" @close)

("{" @open
  "}" @close)

(("\"" @open
  "\"" @close)
  (#set! rainbow.exclude))

(("'" @open
  "'" @close)
  (#set! rainbow.exclude))
)SCM";

// ---------------------------------------------------------------------------
// indents.scm — Auto-indentation rules
// ---------------------------------------------------------------------------
constexpr const char* INDENTS_SCM = R"SCM(
[
  (field_expression)
  (assignment_expression)
  (init_declarator)
  (if_statement)
  (for_statement)
  (while_statement)
  (do_statement)
  (else_clause)
] @indent

(_
  "{"
  "}" @end) @indent

(_
  "("
  ")" @end) @indent

((comment) @indent
  (#match? @indent "^/\\*"))

(if_statement) @start.if

(for_statement) @start.for

(while_statement) @start.while

(do_statement) @start.do

(switch_statement) @start.switch

(else_clause) @start.else
)SCM";

// ---------------------------------------------------------------------------
// injections.scm — Language injections (e.g. doxygen in comments)
// ---------------------------------------------------------------------------
constexpr const char* INJECTIONS_SCM = R"SCM(
((comment) @injection.content
  (#set! injection.language "comment"))

((comment) @injection.content
  (#match? @injection.content "^(///|//!|/\\*\\*|/\\*!)(.*)") 
  (#set! injection.language "doxygen")
  (#set! injection.include-children))

(preproc_def
  value: (preproc_arg) @injection.content
  (#set! injection.language "c"))

(preproc_function_def
  value: (preproc_arg) @injection.content
  (#set! injection.language "c"))
)SCM";

// ---------------------------------------------------------------------------
// outline.scm — Code outline / symbol extraction
// ---------------------------------------------------------------------------
constexpr const char* OUTLINE_SCM = R"SCM(
(preproc_def
  "#define" @context
  name: (_) @name) @item

(preproc_function_def
  "#define" @context
  name: (_) @name
  parameters: (preproc_params
    "(" @context
    ")" @context)) @item

(struct_specifier
  "struct" @context
  name: (_) @name) @item

(union_specifier
  "union" @context
  name: (_) @name) @item

(enum_specifier
  "enum" @context
  name: (_) @name) @item

(enumerator
  name: (_) @name) @item

(field_declaration
  type: (_) @context
  declarator: (field_identifier) @name) @item

(type_definition
  "typedef" @context
  declarator: (_) @name) @item

(declaration
  (type_qualifier)? @context
  type: (_)? @context
  declarator: [
    (function_declarator
      declarator: (_) @name
      parameters: (parameter_list
        "(" @context
        ")" @context))
    (pointer_declarator
      "*" @context
      declarator: (function_declarator
        declarator: (_) @name
        parameters: (parameter_list
          "(" @context
          ")" @context)))
    (pointer_declarator
      "*" @context
      declarator: (pointer_declarator
        "*" @context
        declarator: (function_declarator
          declarator: (_) @name
          parameters: (parameter_list
            "(" @context
            ")" @context))))
  ]) @item

(function_definition
  (type_qualifier)? @context
  type: (_)? @context
  declarator: [
    (function_declarator
      declarator: (_) @name
      parameters: (parameter_list
        "(" @context
        ")" @context))
    (pointer_declarator
      "*" @context
      declarator: (function_declarator
        declarator: (_) @name
        parameters: (parameter_list
          "(" @context
          ")" @context)))
    (pointer_declarator
      "*" @context
      declarator: (pointer_declarator
        "*" @context
        declarator: (function_declarator
          declarator: (_) @name
          parameters: (parameter_list
            "(" @context
            ")" @context))))
  ]) @item

(comment) @annotation
)SCM";

// ---------------------------------------------------------------------------
// textobjects.scm — Vim-style text object selections (functions, classes, etc.)
// ---------------------------------------------------------------------------
constexpr const char* TEXTOBJECTS_SCM = R"SCM(
(declaration
  declarator: (function_declarator)) @function.around

(function_definition
  body: (_
    "{"
    (_)* @function.inside
    "}")) @function.around

(preproc_function_def
  value: (_) @function.inside) @function.around

(comment) @comment.around

(struct_specifier
  body: (_
    "{"
    (_)* @class.inside
    "}")) @class.around

(enum_specifier
  body: (_
    "{"
    [
      (_)
      ","?
    ]* @class.inside
    "}")) @class.around

(union_specifier
  body: (_
    "{"
    (_)* @class.inside
    "}")) @class.around
)SCM";
