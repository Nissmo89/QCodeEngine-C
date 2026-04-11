#pragma once
#include <QColor>
#include <QCompleter>
#include <QTimer>
#include <QHash>
#include <QStringList>

class QPlainTextEdit;
class QKeyEvent;
class QStandardItemModel;
class QStyledItemDelegate;

struct QEditorTheme;

/// Code completion: C keywords, user keywords, and identifiers from the current document
/// (QScintilla-style), with a styled popup.
class AutoCompleter : public QCompleter {
    Q_OBJECT
public:
    explicit AutoCompleter(QObject* parent = nullptr);
    ~AutoCompleter() override;

    void setEditor(QPlainTextEdit* editor);
    void setCustomKeywords(const QStringList& keywords);
    void addCustomKeyword(const QString& keyword);
    void setPopupTheme(const QEditorTheme& theme);

    void refreshModel();
    void updatePopup();

    bool handleKeyPress(QKeyEvent* e);

private slots:
    void insertCompletion(const QString& completion);
    void rebuildDocumentIdentifiers();

private:
    QString wordPrefixAtCursor() const;
    void ensurePopupChrome();
    void applyPopupChromeFromTheme();
    void applyPopupChromeFromEditorFallback();

    QPlainTextEdit* m_editor = nullptr;
    QStandardItemModel* m_model = nullptr;
    QStyledItemDelegate* m_delegate = nullptr;

    QStringList m_baseKeywords;
    QStringList m_customKeywords;
    /// Last UTF-16 index where each identifier appears in the document (recency ordering).
    QHash<QString, int> m_wordLastIndex;

    QTimer m_rebuildTimer;
    bool m_themeApplied = false;
    QColor m_popupBg;
    QColor m_popupFg;
    QColor m_popupBorder;
    QColor m_popupHlBg;
    QColor m_popupHlFg;
    QColor m_hintKeyword;
    QColor m_hintWord;
    QColor m_hintUser;
};
