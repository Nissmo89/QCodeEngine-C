#pragma once
// ============================================================================
//  LineHighlighter.h  –  QCodeEngine-C
//
//  Feature: Notebook-style line highlighter driven by a special comment tag.
//
//  Syntax (in C source):
//      // {N,#RRGGBB}        highlight the next N lines with that hex color
//      // {N,#RGB}           short 3-digit hex also accepted
//      // {N,#RRGGBBAA}      8-digit hex with alpha channel
//
//  Example:
//      // {5,#FFD700}          ← this line is the anchor; NOT highlighted
//      int foo = 1;            ← highlighted (line 1 of 5)
//      int bar = 2;            ← highlighted (line 2 of 5)
//      ...
//
//  Architecture — same shared-tree pattern as FoldManager:
//    LineHighlighter does NOT own a TSParser.  It connects to the
//    TreeSitterHighlighter::parsed(void*) signal and borrows the
//    already-parsed TSTree* to query (comment) nodes — zero extra
//    parsing cost.
//
//  Application:
//    Highlighted ranges are stored as QList<QTextEdit::ExtraSelection>
//    (FullWidthSelection = true) so they layer cleanly over the syntax
//    highlighting without touching the document data.
//
//    The owner (CodeEditorPrivate) calls:
//      extraSelections() → appends to its merged selection list
//    whenever highlightChanged() fires.
//
//  Performance:
//    • The TS query runs only over comment nodes — O(comments), not O(lines).
//    • Building the ExtraSelection list is O(total highlighted lines).
//    • O(1) lookup: isLineHighlighted(blockNum) via QHash<int,QColor>.
// ============================================================================

#include <QObject>
#include <QList>
#include <QHash>
#include <QColor>
#include <QTextEdit>
#include <QTextDocument>
#include <QPlainTextEdit>
#include <tree_sitter/api.h>

class LineHighlighter : public QObject
{
    Q_OBJECT
public:
    explicit LineHighlighter(QObject* parent = nullptr);
    ~LineHighlighter() override = default;

    LineHighlighter(const LineHighlighter&)            = delete;
    LineHighlighter& operator=(const LineHighlighter&) = delete;

    // ── Setup ────────────────────────────────────────────────────────────────
    // Call once at init, same pattern as FoldManager.
    void setDocument(QTextDocument* doc);
    void setEditor(QPlainTextEdit* editor);

    // ── Called from TreeSitterHighlighter::parsed slot ───────────────────────
    // Borrows treePtr, scans comment nodes, rebuilds highlight map.
    void updateFromTree(void* treePtr, QTextDocument* doc);

    // ── Query ────────────────────────────────────────────────────────────────
    // Returns the background color for blockNumber, or an invalid QColor if
    // the line is not part of any highlight region.
    QColor colorForLine(int blockNumber) const;
    bool   isHighlighted(int blockNumber) const;

    // ── ExtraSelection list ──────────────────────────────────────────────────
    // Returns the pre-built FullWidthSelection list.
    // CodeEditorPrivate appends this to its merged extra-selection list
    // in updateCurrentLineHighlight() whenever highlightChanged() fires.
    const QList<QTextEdit::ExtraSelection>& extraSelections() const
    { return m_selections; }

    // Clears all highlights (e.g. when closing a file).
    void clear();

signals:
    // Emitted after updateFromTree() completes and the selection list has
    // been rebuilt.  Owner should respond by calling
    // updateCurrentLineHighlight() to push the new selections to the editor.
    void highlightChanged();

private:
    // Parse one comment node's text for the {N,#COLOR} tag.
    // Returns true and fills outCount/outColor on success.
    static bool parseTag(const QString& commentText,
                         int& outCount, QColor& outColor);

    void rebuildSelections();

    QTextDocument*   m_doc    = nullptr;
    QPlainTextEdit*  m_editor = nullptr;

    // blockNumber → background color
    QHash<int, QColor> m_lineColors;

    // Pre-built selection list handed to the editor
    QList<QTextEdit::ExtraSelection> m_selections;
};
