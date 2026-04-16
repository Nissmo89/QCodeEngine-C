#pragma once
#include <QPlainTextEdit>
#include <QMap>
#include <QTimer>
#include "CodeEditor/CodeEditor.h"
#include "CodeEditor/GutterWidget.h"
#include "FoldManager.h"
#include <QTextBlock>
#include "TreeSitterHighlighter.h"
#include "AutoCompleter.h"
#include "treesitterhelper.h"

// Forward declarations for future stages
// class QSyntaxHighlighter_TS;
// class QAutoCompleter;
// class QMiniMap;
// class QSearchBar;

class CodeEditorPrivate;

class InnerEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit InnerEditor(CodeEditorPrivate* d, QWidget* parent = nullptr);
    
    QTextBlock firstVisibleBlock() const { return QPlainTextEdit::firstVisibleBlock(); }
    QRectF blockBoundingGeometry(const QTextBlock &block) const { return QPlainTextEdit::blockBoundingGeometry(block); }
    QPointF contentOffset() const { return QPlainTextEdit::contentOffset(); }
    QRectF blockBoundingRect(const QTextBlock &block) const { return QPlainTextEdit::blockBoundingRect(block); }

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    CodeEditorPrivate* d_ptr;
};

class CodeEditorPrivate : public QObject {
    Q_OBJECT
public:
    explicit CodeEditorPrivate(CodeEditor* q, QWidget* parent = nullptr);

  

    void updateCurrentLineHighlight();
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect& rect, int dy);
    void applyFolds();
    void setFoldingEnabled(bool enabled);

    bool handleKeyPress(QKeyEvent* e);
    void updateBracketMatch();
    void indentSelection(bool indent);
    void toggleLineComment();

    CodeEditor* q_ptr;
    InnerEditor* m_editor;
    // GutterWidget* m_gutter;
    FoldManager* m_foldManager;

    QEditorTheme m_theme;
    QMap<int, GutterIconInfo> m_icons;

    // Feature toggles
    bool m_insertSpaces = true;
    int m_tabWidth = 4;
    bool m_autoBracket = true;
    bool m_autoIndent = true;
    bool m_foldingEnabled = false;
    bool m_applyingFolds = false;

    QTimer* m_reparseTimer = nullptr;

    QList<QTextEdit::ExtraSelection> m_bracketSelections;
    QList<QTextEdit::ExtraSelection> m_searchSelections;

    // Future components
    TreeSitterHighlighter* m_highlighter = nullptr;
    AutoCompleter* m_completer = nullptr;
    // QMiniMap* m_miniMap = nullptr;
    // QSearchBar* m_searchBar = nullptr;
    GutterWidget *m_gutter = nullptr;

          // ✅ NEW members
    FloatingListPopup *m_functionPopup = nullptr;
    TreeSitterHelper *m_treeSitterHelper = nullptr;

     // ✅ NEW functions
    void updateFunctionList();
    void onFunctionSelected(int line);

private:

public slots:
    void onCursorPositionChanged();
    void onTextChanged();
    void onGutterFoldClicked(int line, bool folded);
};
