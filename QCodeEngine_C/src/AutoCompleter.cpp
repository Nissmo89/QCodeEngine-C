// AutoCompleter.cpp
//
// Architecture based on CodeWizard's "suggestionBox" pattern
// (github.com/AdamJosephMather/CodeWizard), adapted for QCodeEngine-C.
//
// Key design decisions (directly from CodeWizard):
//   • CompletionPopup is a QListWidget child of the editor viewport –
//     positioned via move(), not a floating top-level window.
//   • showSuggestions() filters, populates, and selects row 0 all
//     in one synchronous call — zero timers, zero async events.
//   • currentSelection is tracked as an int index (CodeWizard: currentSelection)
//     and reset to 0 on every new filter pass.

#include "AutoCompleter.h"
#include "CodeEditor/EditorTheme.h"

#include <algorithm>

#include <QApplication>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QFontMetrics>
#include <QListWidgetItem>
#include <QStyleFactory>
#include <QPainter>

// ═════════════════════════════════════════════════════════════════════════════
//  CompletionPopup
// ═════════════════════════════════════════════════════════════════════════════

CompletionPopup::CompletionPopup(QWidget* parent)
    : QListWidget(parent)
{
    // Crucially: no focus steal — the editor keeps keyboard focus at all times
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    // Visual polish
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);
    setLineWidth(1);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSelectionMode(QAbstractItemView::SingleSelection);

    // Default dark theme (overridden by applyTheme)
    applyTheme(QColor(28, 28, 38), QColor(210, 210, 220),
               QColor(70, 70, 100),  QColor(55, 85, 160),
               QColor(255, 255, 255));

    hide();

    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        m_selection = row(item);
        emit completionAccepted(item->text());
    });
}

void CompletionPopup::applyTheme(QColor bg, QColor fg, QColor border,
                                  QColor hlBg, QColor hlFg)
{
    // Build a complete stylesheet — same technique CodeWizard uses
    const QString style = QStringLiteral(
        "QListWidget {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 6px;"
        "  padding: 3px;"
        "  outline: none;"
        "}"
        "QListWidget::item {"
        "  padding: 2px 8px;"
        "  border-radius: 3px;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: %4;"
        "  color: %5;"
        "}"
        "QScrollBar:vertical {"
        "  width: 6px;"
        "  background: transparent;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: %3;"
        "  border-radius: 3px;"
        "  min-height: 12px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0;"
        "}"
    )
    .arg(bg.name(QColor::HexArgb))
    .arg(fg.name(QColor::HexArgb))
    .arg(border.name(QColor::HexArgb))
    .arg(hlBg.name(QColor::HexArgb))
    .arg(hlFg.name(QColor::HexArgb));

    setStyleSheet(style);
}

// ── Core: filter + show ───────────────────────────────────────────────────────

void CompletionPopup::showSuggestions(const QList<Entry>& all,
                                       const QString& prefix,
                                       const QRect& cursorRect)
{
    if (prefix.isEmpty()) {
        hide();
        return;
    }

    // Filter: startsWith(prefix, CaseInsensitive) and not exactly equal
    m_visible.clear();
    const QString lp = prefix.toLower();
    for (const Entry& e : all) {
        const QString lt = e.text.toLower();
        if (lt.startsWith(lp) && e.text != prefix)
            m_visible.append(e.text);
    }

    if (m_visible.isEmpty()) {
        hide();
        return;
    }

    // (Re)populate the QListWidget synchronously — CodeWizard's fillSuggestions()
    clear();
    for (const QString& s : m_visible)
        addItem(s);

    // Always reset to row 0 — the CodeWizard invariant
    m_selection = 0;
    setCurrentRow(m_selection);
    update();

    // Size: clamp to 10 rows maximum (CodeWizard uses 10)
    QFontMetrics fm(font());
    const int rowH  = fm.height() + 6;
    const int rows  = qMin(m_visible.size(), 10);
    const int boxW  = 300;
    const int boxH  = rowH * rows + 8;
    resize(boxW, boxH);

    reposition(cursorRect);
    show();
}

void CompletionPopup::reposition(const QRect& cursorRect)
{
    // cursorRect is in viewport coordinates; parent() is the viewport
    QWidget* par = parentWidget();
    if (!par) return;

    const QRect parRect = par->rect();
    int x = cursorRect.left();
    int y = cursorRect.bottom() + 2;

    // Clamp horizontally
    if (x + width() > parRect.right())
        x = parRect.right() - width();
    x = qMax(0, x);

    // Flip above cursor if too close to bottom
    if (y + height() > parRect.bottom())
        y = cursorRect.top() - height() - 2;
    y = qMax(0, y);

    move(x, y);
}

void CompletionPopup::stepSelection(int delta)
{
    if (m_visible.isEmpty()) return;
    m_selection = (m_selection + delta + m_visible.size()) % m_visible.size();
    setCurrentRow(m_selection);
    scrollToItem(item(m_selection));
}

QString CompletionPopup::currentCompletion() const
{
    if (m_visible.isEmpty()) return {};
    if (m_selection < 0 || m_selection >= m_visible.size()) return m_visible.first();
    return m_visible[m_selection];
}

// ═════════════════════════════════════════════════════════════════════════════
//  Helpers
// ═════════════════════════════════════════════════════════════════════════════

namespace {

static const QRegularExpression kIdentRe(
    QStringLiteral(R"(\b[A-Za-z_][A-Za-z0-9_]*\b)"));

static bool isReservedKeyword(const QString& w, const QStringList& kws) {
    for (const QString& k : kws)
        if (k.compare(w, Qt::CaseInsensitive) == 0) return true;
    return false;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
//  AutoCompleter
// ═════════════════════════════════════════════════════════════════════════════

AutoCompleter::AutoCompleter(QObject* parent)
    : QObject(parent)
{
    m_baseKeywords = {
        // C89 / C99 / C11 / C23 keywords
        "auto", "break", "case", "char", "const", "continue", "default", "do",
        "double", "else", "enum", "extern", "float", "for", "goto", "if",
        "inline", "int", "long", "register", "restrict", "return", "short",
        "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
        "unsigned", "void", "volatile", "while",
        "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
        "_Noreturn", "_Static_assert", "_Thread_local",
        // Common macros / identifiers always visible
        "NULL", "nullptr", "true", "false",
        "int8_t", "int16_t", "int32_t", "int64_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t",
        "size_t", "ptrdiff_t", "intptr_t", "uintptr_t",
        "FILE", "NULL",
    };

    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(300);
    connect(&m_rebuildTimer, &QTimer::timeout,
            this, &AutoCompleter::rebuildDocumentIdentifiers);
}

AutoCompleter::~AutoCompleter()
{
    // m_popup is a child of the viewport so Qt deletes it automatically
}

// ── Editor attachment ─────────────────────────────────────────────────────────

void AutoCompleter::setEditor(QPlainTextEdit* editor)
{
    if (m_editor) {
        m_editor->viewport()->removeEventFilter(this);
        disconnect(m_editor->document(), nullptr, this, nullptr);
    }

    m_editor = editor;
    if (!m_editor) return;

    // Parent the popup to the viewport so coordinates match cursorRect()
    m_popup = new CompletionPopup(m_editor->viewport());
    m_popup->hide();
    m_popup->setFont(m_editor->font());

    connect(m_popup, &CompletionPopup::completionAccepted,
            this, &AutoCompleter::onCompletionAccepted);

    connect(m_editor->document(), &QTextDocument::contentsChanged, this, [this]() {
        m_rebuildTimer.start();
    });

    applyThemeToPopup();
    rebuildDocumentIdentifiers();
}

// ── Theme ─────────────────────────────────────────────────────────────────────

void AutoCompleter::setPopupTheme(const QEditorTheme& theme)
{
    m_themeApplied = true;
    m_popupBg    = theme.background.lighter(108);
    m_popupFg    = theme.foreground;
    m_popupBorder= theme.gutterBorderColor.isValid()
                       ? theme.gutterBorderColor
                       : theme.foreground.darker(200);
    m_popupHlBg  = theme.selectionBackground;
    m_popupHlFg  = theme.selectionForeground;
    applyThemeToPopup();
}

void AutoCompleter::applyThemeToPopup()
{
    if (!m_popup) return;

    if (!m_themeApplied && m_editor) {
        const QPalette ep = m_editor->palette();
        m_popupBg    = ep.color(QPalette::Base).lighter(108);
        m_popupFg    = ep.color(QPalette::Text);
        m_popupBorder= ep.color(QPalette::Mid);
        m_popupHlBg  = ep.color(QPalette::Highlight);
        m_popupHlFg  = ep.color(QPalette::HighlightedText);
    }

    m_popup->applyTheme(m_popupBg, m_popupFg, m_popupBorder,
                        m_popupHlBg, m_popupHlFg);
    if (m_editor) m_popup->setFont(m_editor->font());
}

// ── Keyword management ────────────────────────────────────────────────────────

void AutoCompleter::setCustomKeywords(const QStringList& keywords)
{
    m_customKeywords = keywords;
    rebuildEntries();
}

void AutoCompleter::addCustomKeyword(const QString& keyword)
{
    if (keyword.isEmpty()) return;
    for (const QString& k : m_customKeywords)
        if (k.compare(keyword, Qt::CaseInsensitive) == 0) return;
    m_customKeywords.append(keyword);
    rebuildEntries();
}

// ── Model building ────────────────────────────────────────────────────────────

void AutoCompleter::refreshModel()
{
    rebuildEntries();
}

void AutoCompleter::rebuildEntries()
{
    m_entries.clear();

    // 1) Document identifiers sorted by recency (most recently seen first)
    struct DocEntry { QString text; int lastIdx; };
    QVector<DocEntry> docVec;
    docVec.reserve(m_wordLastIndex.size());

    for (auto it = m_wordLastIndex.constBegin(); it != m_wordLastIndex.constEnd(); ++it) {
        const QString& w = it.key();
        if (w.size() < 2) continue;
        if (isReservedKeyword(w, m_baseKeywords)) continue;
        bool customHit = false;
        for (const QString& c : m_customKeywords)
            if (c.compare(w, Qt::CaseInsensitive) == 0) { customHit = true; break; }
        if (customHit) continue;
        docVec.append({w, it.value()});
    }

    std::sort(docVec.begin(), docVec.end(), [](const DocEntry& a, const DocEntry& b) {
        if (a.lastIdx != b.lastIdx) return a.lastIdx > b.lastIdx;
        return a.text.compare(b.text, Qt::CaseInsensitive) < 0;
    });

    using Kind = CompletionPopup::Kind;
    for (const DocEntry& e : docVec)
        m_entries.append({e.text, Kind::DocumentWord});

    // 2) C keywords, alphabetically
    QStringList kw = m_baseKeywords;
    std::sort(kw.begin(), kw.end(),
              [](const QString& a, const QString& b){ return a.compare(b, Qt::CaseInsensitive) < 0; });
    for (const QString& k : kw)
        m_entries.append({k, Kind::CKeyword});

    // 3) User keywords, alphabetically, no dupes with base
    QStringList ck = m_customKeywords;
    std::sort(ck.begin(), ck.end(),
              [](const QString& a, const QString& b){ return a.compare(b, Qt::CaseInsensitive) < 0; });
    for (const QString& k : ck) {
        bool dup = false;
        for (const QString& b : m_baseKeywords)
            if (b.compare(k, Qt::CaseInsensitive) == 0) { dup = true; break; }
        if (!dup) m_entries.append({k, Kind::UserKeyword});
    }
}

void AutoCompleter::rebuildDocumentIdentifiers()
{
    if (!m_editor || !m_editor->document()) return;

    const QString t = m_editor->toPlainText();
    QHash<QString, int> lastIdx;
    QRegularExpressionMatchIterator it = kIdentRe.globalMatch(t);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        lastIdx.insert(m.captured(0), m.capturedStart());
    }
    m_wordLastIndex = std::move(lastIdx);
    rebuildEntries();
}

// ── Prefix helper (identical to CodeWizard's getCurrentWord) ──────────────────

QString AutoCompleter::wordPrefixAtCursor() const
{
    if (!m_editor) return {};

    QTextCursor tc   = m_editor->textCursor();
    QTextBlock  bl   = tc.block();
    const QString bt = bl.text();
    int blockPos     = tc.position() - bl.position();

    int start = blockPos - 1;
    while (start >= 0 && (bt[start].isLetterOrNumber() || bt[start] == QLatin1Char('_')))
        --start;

    return bt.mid(start + 1, blockPos - start - 1);
}

// ── Insertion (CodeWizard's insertCompletion — strip prefix, then insert) ─────

void AutoCompleter::insertCompletion(const QString& completion)
{
    if (!m_editor || completion.isEmpty()) return;

    const QString prefix = wordPrefixAtCursor();
    // Insert only the suffix that the user hasn't typed yet
    const QString suffix = completion.startsWith(prefix, Qt::CaseInsensitive)
                               ? completion.mid(prefix.size())
                               : completion;

    QTextCursor tc = m_editor->textCursor();
    tc.insertText(suffix);
    m_editor->setTextCursor(tc);

    if (m_popup) m_popup->hide();
}

void AutoCompleter::onCompletionAccepted(const QString& text)
{
    insertCompletion(text);
}

// ── Main entry points ─────────────────────────────────────────────────────────

void AutoCompleter::updatePopup()
{
    if (!m_editor || !m_popup) return;

    if (!m_themeApplied) applyThemeToPopup();
    m_popup->setFont(m_editor->font());

    const QString prefix = wordPrefixAtCursor();

    // cursorRect() is already in viewport coordinates
    m_popup->showSuggestions(m_entries, prefix, m_editor->cursorRect());
}

bool AutoCompleter::handleKeyPress(QKeyEvent* e)
{
    if (!m_popup || !m_popup->isVisible()) {
        // Ctrl+Space: force-open popup
        if (e->modifiers().testFlag(Qt::ControlModifier) && e->key() == Qt::Key_Space) {
            updatePopup();
            return true;
        }
        return false;
    }

    switch (e->key()) {

    // ── Accept ────────────────────────────────────────────────────────────
    case Qt::Key_Tab:
    case Qt::Key_Enter:
    case Qt::Key_Return: {
        const QString pick = m_popup->currentCompletion();
        if (!pick.isEmpty()) {
            insertCompletion(pick);
            e->accept();
            return true;
        }
        m_popup->hide();
        return false;
    }

    // ── Dismiss ───────────────────────────────────────────────────────────
    case Qt::Key_Escape:
        m_popup->hide();
        e->accept();
        return true;

    // ── Navigation (CodeWizard: J/K for down/up in normal mode; here arrows)
    case Qt::Key_Down:
        m_popup->stepSelection(+1);
        e->accept();
        return true;

    case Qt::Key_Up:
        m_popup->stepSelection(-1);
        e->accept();
        return true;

    case Qt::Key_PageDown:
        m_popup->stepSelection(+5);
        e->accept();
        return true;

    case Qt::Key_PageUp:
        m_popup->stepSelection(-5);
        e->accept();
        return true;

    default:
        // Any other key: let the editor handle it; updatePopup() is called
        // by InnerEditor::keyPressEvent after the base class processes it.
        return false;
    }
}
