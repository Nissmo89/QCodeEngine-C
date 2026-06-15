find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
enable_testing()

function(add_editor_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE QCodeEngine_C Qt6::Test Qt6::Widgets)
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tree-sitter/lib/include
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_editor_test(test_findreplace_crash)
add_editor_test(test_bracket_guides_editing)
add_editor_test(test_live_indentation)
add_editor_test(test_theme_json)
add_editor_test(test_autocomplete_behavior)
add_editor_test(test_autocomplete_symbols)
add_editor_test(test_editor_layout)
add_editor_test(test_minimap_behavior)
add_editor_test(test_document_symbols)
