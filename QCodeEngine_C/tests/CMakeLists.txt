find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
enable_testing()

function(add_editor_test name)
    # Stub for now
    # add_executable(${name} ${name}.cpp)
    # target_link_libraries(${name} QCodeEditor Qt6::Test Qt6::Widgets)
    # add_test(NAME ${name} COMMAND ${name})
endfunction()

# add_editor_test(test_highlighting)
# add_editor_test(test_folding)
# add_editor_test(test_autocomplete)
# add_editor_test(test_theming)
# add_editor_test(test_search)
# add_editor_test(test_performance)
