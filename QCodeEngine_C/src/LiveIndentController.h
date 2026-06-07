#pragma once

#include <QString>
#include <tree_sitter/api.h>

#include "CodeEditor/EditorTypes.h"

class QKeyEvent;
class QPlainTextEdit;
class QTextCursor;

class TreeSitterHighlighter;

class LiveIndentController
{
public:
    LiveIndentController(QPlainTextEdit* editor, TreeSitterHighlighter* highlighter);
    ~LiveIndentController();

    void setEnabled(bool enabled);
    void setTabWidth(int tabWidth);
    void setInsertSpaces(bool insertSpaces);
    void setAutoBracketEnabled(bool enabled);
    void setStylePreset(IndentStylePreset preset);

    bool handleKeyPress(QKeyEvent* event);
    bool handleKeyPress(QKeyEvent* event, QTextCursor& cursor, bool ownEditBlock);

private:
    int safeTabWidth() const;
    int indentationColumnsForRow(int row) const;
    int fallbackIndentationForRow(int row) const;
    int indentationColumnsForNewLine(const QTextCursor& cursor) const;
    int indentationColumnsForClosingLine(const QTextCursor& cursor) const;
    int allmanOpenBraceIndentation(const QTextCursor& cursor) const;
    int previousLessIndentedColumns(int fromBlockNumber, int currentColumns) const;

    bool handleNewline(QTextCursor& cursor);
    bool handleClosingBrace(QTextCursor& cursor);
    bool handleAllmanOpenBrace(QTextCursor& cursor);
    bool handleBackspace(QTextCursor& cursor);

    bool queryIndentationLevelForRow(int row, int* outLevel) const;
    bool matchPredicatesPass(const TSQueryMatch& match, const QString& source) const;
    bool evaluatePredicate(const TSQueryPredicateStep* steps,
                           uint32_t stepCount,
                           const TSQueryMatch& match,
                           const QString& source) const;
    QString captureText(const TSQueryCapture& capture, const QString& source) const;
    bool shouldIgnoreIndentNode(TSNode node) const;

    void applyIndentationToBlock(int blockNumber, int columns);
    QString indentationString(int columns) const;
    int leadingWhitespaceColumns(const QString& text) const;
    int leadingWhitespaceLength(const QString& text) const;
    QString currentLineIndentText(const QTextCursor& cursor) const;
    bool linePrefixIsWhitespaceOnly(const QString& text) const;
    bool lineEndsWithOpeningToken(const QString& text) const;
    bool shouldIncreaseIndentAfterLine(const QString& text) const;
    int previousNonEmptyIndentation(int fromBlockNumber) const;

    QPlainTextEdit* m_editor = nullptr;
    TreeSitterHighlighter* m_highlighter = nullptr;
    TSQuery* m_query = nullptr;
    bool m_enabled = true;
    bool m_insertSpaces = true;
    bool m_autoBracketEnabled = true;
    int m_tabWidth = 4;
    IndentStylePreset m_stylePreset = IndentStylePreset::KR;
};
