#pragma once
#include <QWidget>
#include "EditorTheme.h"
#include "EditorTypes.h"
#include "GutterWidget.h"

class CodeEditorPrivate;

class CodeEditor : public QWidget {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);
    ~CodeEditor();

    // --- Content ---
    void        setText(const QString& text);
    QString     text() const;
    void        insertText(const QString& text);
    void        clear();

    // --- File operations ---
    bool        loadFile(const QString& filePath);
    bool        saveFile(const QString& filePath);

    // --- Theming ---
    void        setTheme(const QEditorTheme& theme);
    void        setThemeFromFile(const QString& jsonPath);
    QEditorTheme theme() const;

    // --- Font ---
    void        setEditorFont(const QFont& font);
    QFont       editorFont() const;

    // --- Features toggles ---
    void        setLineNumbersVisible(bool visible);
    void        setMiniMapVisible(bool visible);
    void        setFoldingEnabled(bool enabled);
    void        setAutoCompleteEnabled(bool enabled);
    void        setAutoIndentEnabled(bool enabled);
    void        setAutoBracketEnabled(bool enabled);
    void        setWordWrap(bool enabled);
    void        setShowWhitespace(bool visible);
    void        setTabWidth(int spaces);
    void        setInsertSpacesOnTab(bool spaces);

    // --- Gutter icons (diagnostics, tips) ---
    void        addGutterIcon(int line, GutterIconType type, const QString& tooltip);
    void        removeGutterIcon(int line);
    void        clearGutterIcons();

    // --- Folding ---
    void        foldLine(int line);
    void        unfoldLine(int line);
    void        foldAll();
    void        unfoldAll();

    // --- Search & Replace ---
    void        showSearchBar();
    void        hideSearchBar();
    int         findNext(const QString& term, bool caseSensitive = false, bool regex = false);
    int         findPrev(const QString& term, bool caseSensitive = false, bool regex = false);
    void        replaceNext(const QString& term, const QString& replacement);
    void        replaceAll(const QString& term, const QString& replacement);

    // --- Cursor & Selection ---
    void        goToLine(int line);
    int         currentLine() const;
    int         currentColumn() const;
    QString     selectedText() const;
    void        selectAll();

    // --- Autocomplete custom keywords ---
    void        setCustomKeywords(const QStringList& keywords);
    void        addCustomKeyword(const QString& keyword);

    // --- Read-only ---
    void        setReadOnly(bool readOnly);
    bool        isReadOnly() const;

signals:
    void textChanged();
    void cursorPositionChanged(int line, int column);
    void gutterIconClicked(int line, GutterIconType type);
    void foldToggled(int line, bool folded);
    void fileSaved(const QString& path);
    void fileLoaded(const QString& path);
    void selectionChanged(int startLine, int startCol, int endLine, int endCol);
    void searchMatchCountChanged(int current, int total);
    void documentModifiedChanged(bool modified);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    Q_DECLARE_PRIVATE(CodeEditor)
    QScopedPointer<CodeEditorPrivate> d_ptr;
};
