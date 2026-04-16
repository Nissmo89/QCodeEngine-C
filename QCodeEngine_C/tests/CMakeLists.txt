find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
enable_testing()

# ---------------------------------------------------------------------------
# Helper to add a QCodeEngine test executable
# ---------------------------------------------------------------------------
function(add_editor_test name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tree-sitter/lib/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tree-sitter-c/bindings/c
    )
    target_link_libraries(${name}
        PRIVATE
            QCodeEngine_C
            tree-sitter-lib
            Qt6::Test
            Qt6::Widgets
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ---------------------------------------------------------------------------
# Property test: Property 1 — All fold ranges are multi-line
# Feature: zed-style-code-folding, Property 1
# Validates: Requirements 2.6
# ---------------------------------------------------------------------------
add_editor_test(test_fold_query_properties)

# ---------------------------------------------------------------------------
# Property test: Property 2 — Fold range fields match tree-sitter node positions
# Feature: zed-style-code-folding, Property 2
# Validates: Requirements 2.7
# ---------------------------------------------------------------------------
add_editor_test(test_fold_query_property2)

# ---------------------------------------------------------------------------
# Unit tests for FoldQuery
# Feature: zed-style-code-folding, Task 2.5
# Validates: Requirements 2.1, 2.2, 2.3, 2.5, 2.6
# ---------------------------------------------------------------------------
add_editor_test(test_fold_query_unit)

# ---------------------------------------------------------------------------
# FoldManager property tests (Properties 3–8) and unit tests
# Feature: zed-style-code-folding, Tasks 3.3–3.9
# Validates: Requirements 3.1, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.11, 3.13, 8.1, 8.3
#
# FoldManager is in src/ (not the public include/ tree), so we add ../src to
# the include path for this target specifically.
# ---------------------------------------------------------------------------
add_executable(test_fold_manager test_fold_manager.cpp)
target_include_directories(test_fold_manager
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tree-sitter/lib/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tree-sitter-c/bindings/c
)
target_link_libraries(test_fold_manager
    PRIVATE
        QCodeEngine_C
        tree-sitter-lib
        Qt6::Test
        Qt6::Widgets
)
add_test(NAME test_fold_manager COMMAND test_fold_manager)

# ---------------------------------------------------------------------------
# CodeEditorPrivate property tests (Properties 9–12)
# Feature: zed-style-code-folding, Tasks 5.7–5.10
# Validates: Requirements 4.4, 4.7, 5.1, 9.1
# ---------------------------------------------------------------------------
add_executable(test_code_editor_properties test_code_editor_properties.cpp)
target_include_directories(test_code_editor_properties
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tree-sitter/lib/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tree-sitter-c/bindings/c
)
target_link_libraries(test_code_editor_properties
    PRIVATE
        QCodeEngine_C
        tree-sitter-lib
        Qt6::Test
        Qt6::Widgets
)
add_test(NAME test_code_editor_properties COMMAND test_code_editor_properties)

