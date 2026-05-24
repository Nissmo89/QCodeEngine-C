find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
enable_testing()

function(add_editor_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE QCodeEngine_C Qt6::Test Qt6::Widgets)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_editor_test(test_findreplace_crash)
