#include "AutoCompleter.h"
#include "CodeEditor/EditorTheme.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QKeyEvent>
#include <QPainter>
#include <QListView>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTextCursor>
#include <QTextDocument>

namespace {

enum Kind : int { DocumentWord = 0, CKeyword = 1, UserKeyword = 2 };

static const int kKindRole = Qt::UserRole + 1;

static const QRegularExpression kIdentRe(QStringLiteral(R"(\b[A-Za-z_][A-Za-z0-9_]*\b)"));

static bool isReservedKeyword(const QString& w, const QStringList& baseKeywords) {
    for (const QString& k : baseKeywords) {
        if (k.compare(w, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

class CompletionPopupDelegate final : public QStyledItemDelegate {
public:
    QColor m_hintKeyword;
    QColor m_hintWord;
    QColor m_hintUser;

    CompletionPopupDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
        m_hintKeyword = QColor(130, 180, 255);
        m_hintWord = QColor(150, 200, 150);
        m_hintUser = QColor(200, 160, 255);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.isValid())
            return;

        const QString text = index.data(Qt::DisplayRole).toString();
        const int kind = index.data(kKindRole).toInt();

        QString hint;
        QColor hintCol = m_hintWord;
        switch (kind) {
        case CKeyword:
            hint = QStringLiteral("keyword");
            hintCol = m_hintKeyword;
            break;
        case UserKeyword:
            hint = QStringLiteral("user");
            hintCol = m_hintUser;
            break;
        default:
            hint = QStringLiteral("in file");
            hintCol = m_hintWord;
            break;
        }

        painter->save();
        painter->setClipRect(option.rect);

        const bool selected = option.state.testFlag(QStyle::State_Selected);
        if (selected) {
            painter->fillRect(option.rect, option.palette.brush(QPalette::Highlight));
            painter->setPen(option.palette.color(QPalette::HighlightedText));
        } else {
            painter->setPen(option.palette.color(QPalette::Text));
        }

        const QFontMetrics fm(option.font);
        const int padX = 10;
        const int rightW = fm.horizontalAdvance(hint) + padX;

        const QRect r = option.rect.adjusted(padX, 0, -padX, 0);
        const QRect leftRect(r.left(), r.top(), r.width() - rightW, r.height());
        const QRect rightRect(r.right() - rightW + padX, r.top(), rightW, r.height());

        painter->setFont(option.font);
        painter->drawText(leftRect, Qt::AlignVCenter | Qt::AlignLeft, text);

        QColor hc = hintCol;
        if (selected)
            hc = option.palette.color(QPalette::HighlightedText);
        hc.setAlpha(selected ? 220 : 180);
        painter->setPen(hc);
        painter->drawText(rightRect, Qt::AlignVCenter | Qt::AlignRight, hint);

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(index);
        const QFontMetrics fm(option.font);
        return QSize(360, fm.height() + 10);
    }
};

} // namespace

// ── AutoCompleter ────────────────────────────────────────────────────────────

AutoCompleter::AutoCompleter(QObject* parent)
    : QCompleter(parent)
    , m_model(new QStandardItemModel(this))
    , m_delegate(new CompletionPopupDelegate(this))
{
    setModel(m_model);
    setCompletionMode(QCompleter::PopupCompletion);
    setCaseSensitivity(Qt::CaseInsensitive);
    setFilterMode(Qt::MatchStartsWith);
    setCompletionColumn(0);
    setMaxVisibleItems(14);

    m_baseKeywords = {
        "auto", "break", "case", "char", "const", "continue", "default", "do",
        "double", "else", "enum", "extern", "float", "for", "goto", "if",
        "inline", "int", "long", "register", "restrict", "return", "short",
        "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
        "unsigned", "void", "volatile", "while",
        "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
        "_Noreturn", "_Static_assert", "_Thread_local",
    };

    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(250);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &AutoCompleter::rebuildDocumentIdentifiers);

    connect(this, QOverload<const QString&>::of(&QCompleter::activated),
            this, &AutoCompleter::insertCompletion);

    refreshModel();
}

AutoCompleter::~AutoCompleter() = default;

void AutoCompleter::setEditor(QPlainTextEdit* editor) {
    if (m_editor) {
        disconnect(m_editor->document(), nullptr, this, nullptr);
    }

    m_editor = editor;
    setWidget(editor);

    if (!m_editor)
        return;

    connect(m_editor->document(), &QTextDocument::contentsChanged, this, [this]() {
        m_rebuildTimer.start();
    });

    rebuildDocumentIdentifiers();
    ensurePopupChrome();
    applyPopupChromeFromEditorFallback();
}

void AutoCompleter::setPopupTheme(const QEditorTheme& theme) {
    m_themeApplied = true;

    m_popupBg = theme.background.lighter(108);

    m_popupFg = theme.foreground;
    m_popupBorder = theme.gutterBorderColor.isValid() ? theme.gutterBorderColor
                                                      : theme.foreground.darker(180);
    m_popupHlBg = theme.selectionBackground;
    m_popupHlFg = theme.selectionForeground;
    m_hintKeyword = theme.tokenKeyword;
    m_hintWord = theme.tokenIdentifier;
    m_hintUser = theme.tokenPreprocessor.isValid() ? theme.tokenPreprocessor : QColor(200, 160, 255);

    {
        auto* del = static_cast<CompletionPopupDelegate*>(m_delegate);
        del->m_hintKeyword = m_hintKeyword;
        del->m_hintWord = m_hintWord;
        del->m_hintUser = m_hintUser;
    }

    ensurePopupChrome();
    applyPopupChromeFromTheme();
}

void AutoCompleter::setCustomKeywords(const QStringList& keywords) {
    m_customKeywords = keywords;
    refreshModel();
}

void AutoCompleter::addCustomKeyword(const QString& keyword) {
    if (keyword.isEmpty())
        return;
    for (const QString& k : m_customKeywords) {
        if (k.compare(keyword, Qt::CaseInsensitive) == 0)
            return;
    }
    m_customKeywords.append(keyword);
    refreshModel();
}

void AutoCompleter::refreshModel() {
    m_model->clear();

    struct Entry {
        QString text;
        int kind;
        int lastIdx;
    };
    QVector<Entry> docEntries;
    docEntries.reserve(m_wordLastIndex.size());

    for (auto it = m_wordLastIndex.constBegin(); it != m_wordLastIndex.constEnd(); ++it) {
        const QString& w = it.key();
        if (w.size() < 2)
            continue;
        if (isReservedKeyword(w, m_baseKeywords))
            continue;
        bool customHit = false;
        for (const QString& c : m_customKeywords) {
            if (c.compare(w, Qt::CaseInsensitive) == 0) {
                customHit = true;
                break;
            }
        }
        if (customHit)
            continue;

        docEntries.append({w, DocumentWord, it.value()});
    }

    std::sort(docEntries.begin(), docEntries.end(), [](const Entry& a, const Entry& b) {
        if (a.lastIdx != b.lastIdx)
            return a.lastIdx > b.lastIdx;
        return QString::compare(a.text, b.text, Qt::CaseInsensitive) < 0;
    });

    for (const Entry& e : docEntries) {
        auto* item = new QStandardItem(e.text);
        item->setData(e.kind, kKindRole);
        m_model->appendRow(item);
    }

    QStringList kw = m_baseKeywords;
    std::sort(kw.begin(), kw.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
    for (const QString& k : kw) {
        auto* item = new QStandardItem(k);
        item->setData(CKeyword, kKindRole);
        m_model->appendRow(item);
    }

    QStringList cust = m_customKeywords;
    std::sort(cust.begin(), cust.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
    for (const QString& k : cust) {
        bool dup = false;
        for (const QString& b : m_baseKeywords) {
            if (b.compare(k, Qt::CaseInsensitive) == 0) {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;
        auto* item = new QStandardItem(k);
        item->setData(UserKeyword, kKindRole);
        m_model->appendRow(item);
    }
}

void AutoCompleter::rebuildDocumentIdentifiers() {
    if (!m_editor || !m_editor->document())
        return;

    const QString t = m_editor->toPlainText();
    QHash<QString, int> lastIdx;
    QRegularExpressionMatchIterator it = kIdentRe.globalMatch(t);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        lastIdx.insert(m.captured(0), m.capturedStart());
    }

    m_wordLastIndex = std::move(lastIdx);
    refreshModel();
}

QString AutoCompleter::wordPrefixAtCursor() const {
    if (!m_editor)
        return {};

    QTextCursor tc = m_editor->textCursor();
    QString blockText = tc.block().text();
    int col = tc.positionInBlock();
    if (col <= 0)
        return {};

    int start = col;
    while (start > 0) {
        const QChar c = blockText.at(start - 1);
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            --start;
        else
            break;
    }
    return blockText.mid(start, col - start);
}

void AutoCompleter::ensurePopupChrome() {
    QAbstractItemView* pop = popup();
    if (!pop)
        return;

    pop->setObjectName(QStringLiteral("codeCompleterPopup"));
    if (auto* lv = qobject_cast<QListView*>(pop))
        lv->setUniformItemSizes(true);
    pop->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pop->setMouseTracking(true);
    pop->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pop->setSelectionBehavior(QAbstractItemView::SelectRows);
    pop->setSelectionMode(QAbstractItemView::SingleSelection);
    pop->setMinimumWidth(340);
    pop->setMaximumHeight(280);
    pop->setItemDelegate(m_delegate);

    if (m_editor)
        pop->setFont(m_editor->font());
}

void AutoCompleter::applyPopupChromeFromTheme() {
    QAbstractItemView* pop = popup();
    if (!pop)
        return;

    const QString bg = m_popupBg.name(QColor::HexArgb);
    const QString fg = m_popupFg.name(QColor::HexArgb);
    const QString bd = m_popupBorder.name(QColor::HexArgb);
    const QString hl = m_popupHlBg.name(QColor::HexArgb);
    const QString hlf = m_popupHlFg.name(QColor::HexArgb);

    pop->setStyleSheet(QStringLiteral(
        "QAbstractItemView#codeCompleterPopup {"
        "  background: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 8px;"
        "  padding: 4px;"
        "  outline: none;"
        "}"
        "QAbstractItemView#codeCompleterPopup::item {"
        "  min-height: 24px;"
        "  border-radius: 4px;"
        "  padding: 2px 4px;"
        "}"
        "QAbstractItemView#codeCompleterPopup::item:selected {"
        "  background: %4;"
        "  color: %5;"
        "}")
                           .arg(bg, fg, bd, hl, hlf));

    QPalette pal = pop->palette();
    pal.setColor(QPalette::Base, m_popupBg);
    pal.setColor(QPalette::Text, m_popupFg);
    pal.setColor(QPalette::Highlight, m_popupHlBg);
    pal.setColor(QPalette::HighlightedText, m_popupHlFg);
    pop->setPalette(pal);
}

void AutoCompleter::applyPopupChromeFromEditorFallback() {
    if (!m_editor || m_themeApplied)
        return;
    QAbstractItemView* pop = popup();
    if (!pop)
        return;

    const QPalette epal = m_editor->palette();
    m_popupBg = epal.color(QPalette::Base).lighter(108);
    m_popupFg = epal.color(QPalette::Text);
    m_popupBorder = epal.color(QPalette::Mid);
    m_popupHlBg = epal.color(QPalette::Highlight);
    m_popupHlFg = epal.color(QPalette::HighlightedText);
    m_hintKeyword = QColor(130, 180, 255);
    m_hintWord = QColor(160, 200, 160);

    {
        auto* del = static_cast<CompletionPopupDelegate*>(m_delegate);
        del->m_hintKeyword = m_hintKeyword;
        del->m_hintWord = m_hintWord;
        del->m_hintUser = QColor(200, 160, 255);
    }

    applyPopupChromeFromTheme();
}

void AutoCompleter::insertCompletion(const QString& completion) {
    if (!m_editor || m_editor != widget())
        return;

    const QString prefix = wordPrefixAtCursor();
    QTextCursor tc = m_editor->textCursor();
    const int pos = tc.position();

    int n = prefix.size();
    if (n > pos)
        n = pos;

    tc.setPosition(pos - n);
    tc.setPosition(pos, QTextCursor::KeepAnchor);
    tc.insertText(completion);
    m_editor->setTextCursor(tc);
}

void AutoCompleter::updatePopup() {
    if (!m_editor)
        return;

    const QString prefix = wordPrefixAtCursor();
    if (prefix.size() < 1) {
        popup()->hide();
        return;
    }

    ensurePopupChrome();
    if (!m_themeApplied)
        applyPopupChromeFromEditorFallback();

    setCompletionPrefix(prefix);

    QAbstractItemView* pop = popup();
    pop->setCurrentIndex(completionModel()->index(0, 0));

    QRect cr = m_editor->cursorRect();
    const int scrollW = pop->verticalScrollBar()->isVisible() ? pop->verticalScrollBar()->sizeHint().width() : 0;
    const int w = qMax(pop->minimumWidth(), pop->sizeHintForColumn(0) + scrollW + 24);
    cr.setWidth(w);
    complete(cr);
}

bool AutoCompleter::handleKeyPress(QKeyEvent* e) {
    if (!popup()->isVisible()) {
        const bool shortcut = e->modifiers().testFlag(Qt::ControlModifier) && e->key() == Qt::Key_Space;
        if (shortcut) {
            updatePopup();
            return true;
        }
        return false;
    }

    switch (e->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
    case Qt::Key_Tab:
    case Qt::Key_Backtab: {
        QString pick = currentCompletion();
        if (pick.isEmpty()) {
            const QModelIndex idx = popup()->currentIndex();
            if (idx.isValid())
                pick = idx.data(Qt::DisplayRole).toString();
        }
        if (!pick.isEmpty()) {
            insertCompletion(pick);
            popup()->hide();
            e->accept();
            return true;
        }
        popup()->hide();
        e->ignore();
        return false;
    }
    case Qt::Key_Escape:
        popup()->hide();
        e->accept();
        return true;
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Home:
    case Qt::Key_End:
        e->ignore();
        return false;
    default:
        break;
    }

    return false;
}
