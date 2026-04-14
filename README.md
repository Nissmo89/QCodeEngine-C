# QCodeEngine-C

QCodeEngine-C is a C programming-oriented editor widget and library built on **Qt 6** (`QPlainTextEdit`) and **Tree-sitter**. It provides a rich set of features tailored for building robust code editors with a strong focus on high-performance syntax highlighting, code folding, and modern editing aids.

![Theme Preview](Docs/assets/theme.gif)

## Features

- **Tree-sitter Syntax Highlighting**: Fast and accurate C syntax highlighting using the robust `tree-sitter-c` grammar.
- **Advanced Gutter System**: Multi-panel gutter handling line numbers, fold arrows, and margin markers (bookmarks, breakpoints, etc.).
- **Code Folding**: Context-aware code folding powered by syntax tree traversal.
- **Theme Support**: Includes built-in support for multiple themes (One Dark, Dracula, etc.). Use `Ctrl+T` to cycle through themes in the demo.
- **Editor Aids**: Features auto-indentation, auto-bracket pairing, customizable keyword autocomplete, and toggleable comments (`Ctrl+/`).

## Architecture

The project is structured into two main parts:
1. **Application (Demo)**: A lightweight `main.cpp` executable demonstrating the editor widget with theme switching and a sample C document.
2. **QCodeEngine_C Library**: The core library containing the `CodeEditor` (public API) and underlying components:
   - `InnerEditor`: The actual text surface extending `QPlainTextEdit`.
   - `TreeSitterHighlighter`: Handles parsing text via tree-sitter and mapping syntax to styles.
   - `FoldManager`: Computes fold ranges dynamically based on the syntax tree nodes.
   - `GutterWidget`: Composed of modular left-to-right panels (`MarginArea`, `LineNumberArea`, and `FoldArea`).
   - `AutoCompleter`: Provides a fast `QCompleter` interface over customizable keyword lists.

## Build Instructions

**Prerequisites:**
- CMake 3.16 or newer
- Qt 6 (Components: Core, Gui, Widgets)
- C++17 compatible compiler

**Steps to Build:**

```bash
# Clone the repository
git clone https://github.com/Nissmo89/QCodeEngine-C.git
cd QCodeEngine-C

# Create a build directory
mkdir build
cd build

# Configure and compile using CMake
cmake ..
cmake --build .

# Run the demo application
./QCodeEngine-C
```

## Roadmap & Upcoming Features

The engine is actively being improved and refined. Here are the features planned for the future:

- **Incremental Parse Corrections**: Improving tree-sitter's incremental updates handling (offset coordinates mapping) to ensure 100% accuracy during heavy edits.
- **Bracket Matching**: Highlighting the corresponding partner when the cursor is positioned next to an opening or closing bracket.
- **Search and Replace API**: Completing the underlying engine to fully support robust searching and line-by-line replacing.
- **Enhancing AutoCompleter**: Wiring custom keyword API to let host applications inject dynamic C-keywords and auto-completion snippets.
- **Code Minimap**: Finalizing and exposing the Minimap API for a sublime-like code overview map.
- **Automated Tests**: Adding comprehensive unit tests to ensure stability across highlighter features.
