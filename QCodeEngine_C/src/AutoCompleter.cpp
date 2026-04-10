#include "AutoCompleter.h"
#include <QPlainTextEdit>
#include <QKeyEvent>
#include <QAbstractItemView>
#include <QScrollBar>

AutoCompleter::AutoCompleter(QObject* parent)
    : QCompleter(parent), m_model(new QStringListModel(this)) 
{
    setModel(m_model);
    setCompletionMode(QCompleter::PopupCompletion);
    setCaseSensitivity(Qt::CaseInsensitive);

    m_baseKeywords = {
        "auto", "break", "case", "char", "const", "continue", "default", "do",
        "double", "else", "enum", "extern", "float", "for", "goto", "if",
        "inline", "int", "long", "register", "restrict", "return", "short",
        "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
        "unsigned", "void", "volatile", "while"
    };
    updateCompletionModel();

    connect(this, QOverload<const QString&>::of(&QCompleter::activated),
            this, &AutoCompleter::insertCompletion);
}

void AutoCompleter::setEditor(QPlainTextEdit* editor) {
    m_editor = editor;
    setWidget(editor);
}

void AutoCompleter::setCustomKeywords(const QStringList& keywords) {
    m_customKeywords = keywords;
    updateCompletionModel();
}

void AutoCompleter::addCustomKeyword(const QString& keyword) {
    if (!m_customKeywords.contains(keyword)) {
        m_customKeywords.append(keyword);
        updateCompletionModel();
    }
}

void AutoCompleter::updateCompletionModel() {
    QStringList all = m_baseKeywords;
    all.append(m_customKeywords);
    all.removeDuplicates();
    all.sort();
    m_model->setStringList(all);
}

void AutoCompleter::insertCompletion(const QString& completion) {
    if (m_editor != widget()) return;
    QTextCursor tc = m_editor->textCursor();
    int extra = completion.length() - completionPrefix().length();
    tc.movePosition(QTextCursor::Left);
    tc.movePosition(QTextCursor::EndOfWord);
    tc.insertText(completion.right(extra));
    m_editor->setTextCursor(tc);
}

QString AutoCompleter::textUnderCursor() const {
    QTextCursor tc = m_editor->textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    return tc.selectedText();
}

bool AutoCompleter::handleKeyPress(QKeyEvent* e) {
    if (popup()->isVisible()) {
        switch (e->key()) {
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Escape:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                e->ignore();
                return true; 
            default:
                break;
        }
    }

    bool isShortcut = (e->modifiers().testFlag(Qt::ControlModifier) && e->key() == Qt::Key_Space);
    if (isShortcut) {
        updatePopup();
        return true;
    }

    return false;
}

void AutoCompleter::updatePopup() {
    if (!m_editor) return;
    QString prefix = textUnderCursor();
    if (prefix.length() < 1) { // 1 char to complete
        popup()->hide();
        return;
    }
    if (prefix != completionPrefix()) {
        setCompletionPrefix(prefix);
        popup()->setCurrentIndex(completionModel()->index(0, 0));
    }
    QRect cr = m_editor->cursorRect();
    cr.setWidth(popup()->sizeHintForColumn(0) + popup()->verticalScrollBar()->sizeHint().width());
    complete(cr);
}
