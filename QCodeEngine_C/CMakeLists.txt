cmake_minimum_required(VERSION 3.16)
project(QCodeEngine_C VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# ── tree-sitter (compiled as object lib) ──────────────────────
add_library(tree-sitter-lib STATIC
    third_party/tree-sitter/lib/src/lib.c
    third_party/tree-sitter-c/src/parser.c
)
target_include_directories(tree-sitter-lib PUBLIC
    third_party/tree-sitter/lib/include
    third_party/tree-sitter/lib/src
)
set_target_properties(tree-sitter-lib PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
)

# ── QCodeEditor library ───────────────────────────────────────
add_library(QCodeEngine_C
    src/CodeEditor.cpp
    src/GutterWidget.cpp
    src/EditorTheme.cpp
    src/FoldManager.cpp
    src/TreeSitterHighlighter.cpp
    include/CodeEditor/CodeEditor.h
    include/CodeEditor/GutterWidget.h
    src/CodeEditor_p.h
    src/FoldManager.h
    src/TreeSitterHighlighter.h
    src/AutoCompleter.cpp
    src/AutoCompleter.h
#    src/QMiniMap.cpp
#    src/QSearchBar.cpp
)

target_include_directories(QCodeEngine_C
    PUBLIC  include/
    PRIVATE src/
)

target_link_libraries(QCodeEngine_C
    PUBLIC  Qt6::Widgets
    PRIVATE tree-sitter-lib
)

# ── Tests ────────────────────────────────────────────────────
option(QCodeEngine_C_BUILD_TESTS "Build test suite" ON)
if(QCodeEngine_C_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ── Install ──────────────────────────────────────────────────
install(TARGETS QCodeEngine_C LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY themes/  DESTINATION share/QCodeEngine_C/themes)
