#pragma once
#include <QCompleter>
#include <QStringListModel>
#include <QStringList>

class QPlainTextEdit;
class QKeyEvent;

class AutoCompleter : public QCompleter {
    Q_OBJECT
public:
    explicit AutoCompleter(QObject* parent = nullptr);

    void setEditor(QPlainTextEdit* editor);
    void setCustomKeywords(const QStringList& keywords);
    void addCustomKeyword(const QString& keyword);
    void updateCompletionModel();
    void updatePopup();

    bool handleKeyPress(QKeyEvent* e);

private slots:
    void insertCompletion(const QString& completion);

private:
    QString textUnderCursor() const;

    QPlainTextEdit* m_editor = nullptr;
    QStringListModel* m_model;
    QStringList m_baseKeywords;
    QStringList m_customKeywords;
};
