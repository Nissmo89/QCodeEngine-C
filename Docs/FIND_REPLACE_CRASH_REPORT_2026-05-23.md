# QCodeEngine-C Crash Report (Find/Replace Path)

Date: 2026-05-23  
Environment: Linux (Ubuntu 24.04), Qt 6.9.2, app run under `gdb`

## Summary

The crash is a `SIGSEGV` inside Qt text layout/paint code:

- `QTextLine::textStart()`
- called from `QPlainTextEdit::paintEvent()`
- entered via `InnerEditor::paintEvent()` at `QCodeEngine_C/src/CodeEditor.cpp:468`

The GTK theme warnings shown before crash are unrelated UI warnings and not the crash source.

## Observed Backtrace (Key Frames)

1. `QTextLine::textStart`
2. `QPlainTextEdit::paintEvent`
3. `InnerEditor::paintEvent` (`CodeEditor.cpp:468`)
4. `QWidget::event`

## Root Cause Analysis

The fold pipeline was mutating block visibility/line-count in a way that can desynchronize `QTextBlock` metadata from `QTextLayout` state.

Primary defect:

- In `FoldManager::applyFoldsToDocument`, this condition was wrong:
  - `if (block.isVisible() == hidden) { ... }`
- For **non-hidden** lines (`hidden == false`) and normal visible blocks (`isVisible == true`), the condition evaluates to true, so line count/visibility was rewritten on nearly every parse/update cycle.

Impact:

- Repeated forced `setLineCount(1)` updates (without robust layout sync semantics) during heavy edit/find-replace churn can leave layout in an inconsistent state.
- Qt paint then hits invalid `QTextLine` data and segfaults in `QTextLine::textStart`.

## Fixes Applied

### 1) Correct fold visibility condition + layout sync

File: `QCodeEngine_C/src/FoldManager.cpp`

- Replaced logic with explicit target visibility (`shouldBeVisible = !hidden`)
- Update only when:
  - visibility differs, or
  - lineCount is inconsistent with visibility
- When updating:
  - call `block.clearLayout()`
  - then set `block.setLineCount(shouldBeVisible ? 1 : 0)`

### 2) Safe unfold normalization when disabling folding

File: `QCodeEngine_C/src/CodeEditor.cpp`

- In `setFoldingEnabled(false)`, when restoring blocks:
  - handle both `!isVisible()` and `lineCount()==0`
  - call `block.clearLayout()` before `setLineCount(1)`

### 3) Earlier stability hardening already in branch

- Removed text-cursor mutation inside `paintEvent`.
- Added guards in tree-sitter change handler for re-entrant/format-only updates.
- Coalesced parsed notifications via event-loop tick to avoid mid-edit fanout.

## Why This Matches the User Repro

Find/Replace operations create rapid document mutations and highlight/fold refreshes.  
The faulty fold condition caused excessive/invalid block line-count rewrites during this churn, which is consistent with “Replace All -> Replace -> edit line -> crash”.

## Validation Plan

1. Build debug:
   - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
   - `cmake --build build -j"$(nproc)"`
2. Run repro manually with folding enabled.
3. Run regression tests:
   - `./build/QCodeEngine_C/tests/test_findreplace_crash`
4. If crash persists:
   - capture `bt full` and `thread apply all bt` in `gdb`.

