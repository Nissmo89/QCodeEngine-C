#pragma once
#include <QObject>
#include <QColor>
#include <QTimer>
#include <QHash>
#include <QStringList>
#include <QListWidget>

class QPlainTextEdit;
class QKeyEvent;
struct QEditorTheme;

// ─────────────────────────────────────────────────────────────────────────────
// CompletionPopup
//
// A QListWidget child of the editor viewport, positioned with move() —
// inspired by CodeWizard's proven "suggestionBox" pattern.
// No Qt::ToolTip trickery, no async show-events, no QCompleter.
// ─────────────────────────────────────────────────────────────────────────────
class CompletionPopup : public QListWidget {
    Q_OBJECT
public:
    enum class Kind { DocumentWord = 0, CKeyword = 1, UserKeyword = 2 };

    struct Entry {
        QString text;
        Kind    kind;
    };

    explicit CompletionPopup(QWidget* parent = nullptr);

    // Replace the master item list and filter immediately by prefix.
    // Always selects row 0. Shows/hides itself.
    void showSuggestions(const QList<Entry>& all, const QString& prefix,
                         const QRect& cursorRect);

    // Step the selection by delta (wraps). Call before hide/insert.
    void stepSelection(int delta);

    // Text of the currently highlighted row. Always valid when isVisible().
    QString currentCompletion() const;

    // Theme / styling
    void applyTheme(QColor bg, QColor fg, QColor border,
                    QColor hlBg, QColor hlFg);

signals:
    void completionAccepted(const QString& text);

private:
    void reposition(const QRect& cursorRect);

    QList<Entry> m_allEntries;
    QStringList  m_visible;    // filtered, parallel to list rows
    int          m_selection = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// AutoCompleter – orchestrator; drop-in for the old QCompleter-based version
// ─────────────────────────────────────────────────────────────────────────────
class AutoCompleter : public QObject {
    Q_OBJECT
public:
    explicit AutoCompleter(QObject* parent = nullptr);
    ~AutoCompleter() override;

    void setEditor(QPlainTextEdit* editor);
    void setCustomKeywords(const QStringList& keywords);
    void addCustomKeyword(const QString& keyword);
    void setPopupTheme(const QEditorTheme& theme);

    // Called by InnerEditor::keyPressEvent — before the base class processes it.
    // Returns true if the event was consumed.
    bool handleKeyPress(QKeyEvent* e);

    // Called by InnerEditor::keyPressEvent — after the base class processes it.
    void updatePopup();

    void refreshModel();

private slots:
    void rebuildDocumentIdentifiers();
    void onCompletionAccepted(const QString& text);

private:
    QString wordPrefixAtCursor() const;
    void    insertCompletion(const QString& text);
    void    applyThemeToPopup();
    void    rebuildEntries();         // rebuild m_entries from all sources

    QPlainTextEdit*  m_editor = nullptr;
    CompletionPopup* m_popup  = nullptr;

    // Word lists
    QStringList          m_baseKeywords;
    QStringList          m_customKeywords;
    QHash<QString, int>  m_wordLastIndex;   // word → last char-position in doc

    QList<CompletionPopup::Entry> m_entries; // master sorted list

    QTimer m_rebuildTimer;

    // Theme
    QColor m_popupBg, m_popupFg, m_popupBorder, m_popupHlBg, m_popupHlFg;
    bool   m_themeApplied = false;
};
