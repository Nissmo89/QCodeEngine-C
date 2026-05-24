#ifndef FINDREPLACEBAR_H
#define FINDREPLACEBAR_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QTextDocument>
#include <QTextCursor>
#include <QRegularExpression>
#include <QTimer>
#include <QGraphicsOpacityEffect>
#include <QScopedValueRollback>
#include <QVector>
#include <functional>
#include <utility>
#include "CodeEditor/EditorTheme.h"

namespace {
static QColor frSurfaceColor(const QEditorTheme &t)
{
    QColor base = t.background.isValid() ? t.background : QColor(30, 30, 30);
    return base.lightness() < 128 ? base.lighter(108) : base.darker(103);
}

static QColor frInputColor(const QEditorTheme &t)
{
    QColor base = t.currentLineBackground.isValid() ? t.currentLineBackground
                                                    : (t.background.isValid() ? t.background
                                                                              : QColor(36, 36, 36));
    return base.lightness() < 128 ? base.lighter(106) : base.darker(104);
}
}

// ─────────────────────────────────────────────────────────────────────────────
// Small styled icon-button used throughout the bar
// ─────────────────────────────────────────────────────────────────────────────
class FRToolButton : public QToolButton {
    Q_OBJECT
public:
    explicit FRToolButton(const QString &text, QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setText(text);
        setCheckable(false);
        setAutoRaise(true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(24);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setFocusPolicy(Qt::TabFocus);
    }

    void setTheme(const QColor &fg, const QColor &hover,
                  const QColor &checked, bool isChecked = false)
    {
        m_fg      = fg;
        m_hover   = hover;
        m_checked = checked;
        m_active  = isChecked;
        update();
    }

    void setActive(bool on) { m_active = on; update(); }
    bool isActive() const   { return m_active; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRect r = rect().adjusted(1, 1, -1, -1);

        if (m_active) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_checked);
            p.drawRoundedRect(r, 4, 4);
        } else if (underMouse()) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_hover);
            p.drawRoundedRect(r, 4, 4);
        }

        p.setPen(m_active ? m_fg.lighter(130) : m_fg);
        p.setFont(font());
        p.drawText(rect(), Qt::AlignCenter, text());
    }

    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *)      override { update(); }

private:
    QColor m_fg, m_hover, m_checked;
    bool   m_active = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Styled line edit — rounded, theme-aware
// ─────────────────────────────────────────────────────────────────────────────
class FRLineEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit FRLineEdit(const QString &placeholder, QWidget *parent = nullptr)
        : QLineEdit(parent)
    {
        setPlaceholderText(placeholder);
        setFixedHeight(26);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFrame(false);
        setContentsMargins(6, 0, 6, 0);
    }

    void setTheme(const QEditorTheme &t) {
        const QColor lineBg = frInputColor(t);
        const QColor border = t.gutterBorderColor.isValid()
                                ? t.gutterBorderColor
                                : QColor(90, 90, 90);
        const QColor fg = t.foreground.isValid() ? t.foreground : QColor(220, 220, 220);
        const QColor selectionBg = t.selectionBackground.isValid()
                                     ? t.selectionBackground
                                     : QColor(88, 112, 178);
        const QColor focusBorder = t.accent.isValid()
                                     ? t.accent
                                     : (t.tokenKeyword.isValid() ? t.tokenKeyword : border.lighter(125));
        const QColor placeholder = t.tokenComment.isValid()
                                     ? t.tokenComment
                                     : fg.darker(130);

        QString qss = QString(
                          "QLineEdit {"
                          "  background: %1;"
                          "  color: %2;"
                          "  border: 1px solid %3;"
                          "  border-radius: 4px;"
                          "  padding: 0 6px;"
                          "  selection-background-color: %4;"
                          "  selection-color: %5;"
                          "}"
                          "QLineEdit::placeholder {"
                          "  color: %6;"
                          "}"
                          "QLineEdit:focus {"
                          "  border: 1px solid %7;"
                          "}"
                          ).arg(lineBg.name(QColor::HexArgb))
                          .arg(fg.name())
                          .arg(border.name())
                          .arg(selectionBg.name())
                          .arg(fg.name())
                          .arg(placeholder.name())
                          .arg(focusBorder.name());
        setStyleSheet(qss);
    }

    // Flashes red briefly when no match found
    void flashError(const QColor &errColor) {
        m_flashColor = errColor;
        m_flash = true;
        update();
        QTimer::singleShot(300, this, [this]{ m_flash = false; update(); });
    }

protected:
    void paintEvent(QPaintEvent *e) override {
        QLineEdit::paintEvent(e);
        if (m_flash) {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            QColor c = m_flashColor;
            c.setAlpha(60);
            p.fillRect(rect(), c);
        }
    }

private:
    QColor m_flashColor;
    bool   m_flash = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// The bar itself
// ─────────────────────────────────────────────────────────────────────────────
class FindReplaceBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit FindReplaceBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        // ── surface config: solid in-editor panel (avoids compositor ghosting) ─
        setAttribute(Qt::WA_StyledBackground, true);
        setFocusPolicy(Qt::StrongFocus);
        if (parentWidget())
            parentWidget()->installEventFilter(this);

        // ── opacity effect for fade-in / fade-out ──────────────────────────
        m_opacity = new QGraphicsOpacityEffect(this);
        m_opacity->setOpacity(1.0);
        setGraphicsEffect(m_opacity);

        m_fadeAnim = new QPropertyAnimation(this, "opacity", this);
        m_fadeAnim->setDuration(120);

        // ── widgets ────────────────────────────────────────────────────────
        buildWidgets();
        buildLayout();
        connectSignals();

        // Search triggers with 80ms debounce
        m_searchTimer = new QTimer(this);
        m_searchTimer->setSingleShot(true);
        m_searchTimer->setInterval(80);
        connect(m_searchTimer, &QTimer::timeout, this, &FindReplaceBar::doHighlightAll);

        setReplaceVisible(false);
        hide();
    }

    // ── Public API ─────────────────────────────────────────────────────────

    void setEditor(QPlainTextEdit *editor) {
        if (m_docChangeConnection)
            QObject::disconnect(m_docChangeConnection);

        m_editor = editor;
        if (!m_editor || !m_editor->document())
            return;

        m_docChangeConnection = connect(m_editor->document(), &QTextDocument::contentsChanged,
                                        this, [this]() {
            if (m_replaceInProgress)
                return;

            if (!isVisible() || m_findEdit->text().isEmpty()) {
                clearHighlights();
                updateMatchLabel();
                return;
            }

            // Drop stale match cursors immediately after any mutation so repaints
            // never run with pre-edit selections while debounce is pending.
            clearHighlightSelectionsOnly();
            m_searchTimer->start();
        });
    }

    void setTheme(const QEditorTheme &theme) {
        m_theme = theme;
        applyTheme();
        update();
    }

    void setHighlightsHandler(std::function<void(const QList<QTextEdit::ExtraSelection>&)> handler) {
        m_highlightsHandler = std::move(handler);
    }

    // 0 disables limit. Useful for very large files to avoid UI stalls.
    void setHighlightAllLimit(int maxHighlights) {
        const int next = qMax(0, maxHighlights);
        if (m_maxHighlights == next)
            return;
        m_maxHighlights = next;
        doHighlightAll();
    }

    // Open the bar in Find-only or Find+Replace mode.
    // Pre-fills the find field with the editor's current selection if any.
    void openFind() {
        setReplaceVisible(false);
        openBar();
    }

    void openFindReplace() {
        setReplaceVisible(true);
        openBar();
    }

    void closeFindBar() {
        closeBar();
    }

    qreal opacity() const {
        return m_opacity ? m_opacity->opacity() : 1.0;
    }

    void setOpacity(qreal v) {
        if (m_opacity) m_opacity->setOpacity(v);
    }

public slots:
    void findNext()     { doFind(false); }
    void findPrevious() { doFind(true);  }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QColor editorBg = m_theme.background.isValid()
                                  ? m_theme.background
                                  : QColor(28, 28, 28);
        p.fillRect(rect(), editorBg);

        // main background panel
        QRectF r = rect().adjusted(8, 4, -8, -4);
        QPainterPath path;
        path.addRoundedRect(r, 6, 6);

        QColor bg = frSurfaceColor(m_theme);
        bg.setAlpha(255);
        p.fillPath(path, bg);

        QColor border = m_theme.gutterBorderColor.isValid()
                            ? m_theme.gutterBorderColor : QColor(80,80,80);
        p.setPen(QPen(border, 1));
        p.drawPath(path);

        // subtle seam so the bar feels attached to editor chrome
        QColor seam = m_theme.gutterBorderColor.isValid()
                        ? m_theme.gutterBorderColor
                        : QColor(70, 70, 70);
        seam.setAlpha(120);
        p.setPen(seam);
        p.drawLine(QPointF(8, 1), QPointF(width() - 8, 1));
    }

    void keyPressEvent(QKeyEvent *e) override {
        switch (e->key()) {
        case Qt::Key_Escape:
            closeBar();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (e->modifiers() & Qt::ShiftModifier)
                findPrevious();
            else
                findNext();
            return;
        case Qt::Key_Tab:
            // Cycle focus: find → replace → options
            focusNextChild();
            return;
        default:
            break;
        }
        QWidget::keyPressEvent(e);
    }

    // Reposition to top-right of parent on resize
    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        repositionToParent();
    }

    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == parentWidget()) {
            switch (event->type()) {
            case QEvent::Resize:
            case QEvent::Show:
            case QEvent::WindowStateChange:
            case QEvent::LayoutRequest:
                repositionToParent();
                break;
            default:
                break;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

private slots:
    // ── search ──────────────────────────────────────────────────────────────

    void onFindTextChanged() {
        m_matchLabel->setText("");
        m_searchTimer->start();
    }

    void doHighlightAll() {
        if (!m_editor) return;
        clearHighlights();

        const QString needle = m_findEdit->text();
        if (needle.isEmpty()) {
            m_matchLabel->setText("");
            return;
        }

        QList<QTextEdit::ExtraSelection> highlights;

        // Match-highlight color: accent color at low opacity
        QColor hlColor = m_theme.selectionBackground.isValid()
                             ? m_theme.selectionBackground
                             : QColor(255, 200, 0, 60);
        hlColor.setAlpha(80);

        QTextDocument *doc = m_editor->document();
        QTextCursor   cur(doc);
        QTextDocument::FindFlags flags = buildFindFlags(false);
        m_highlightsTruncated = false;
        int count = 0, currentIndex = 0;
        QTextCursor editorCursor = m_editor->textCursor();
        int guardPos = -1;

        while (true) {
            cur = findIn(doc, needle, cur, flags);
            if (cur.isNull()) break;
            const int matchStart = cur.selectionStart();
            const int matchEnd = cur.selectionEnd();
            if (matchEnd <= matchStart) {
                const int nextPos = qMin(matchStart + 1, qMax(0, doc->characterCount() - 1));
                if (nextPos <= guardPos)
                    break;
                guardPos = nextPos;
                cur.setPosition(nextPos);
                continue;
            }
            guardPos = matchEnd;
            ++count;

            if (m_maxHighlights > 0 && highlights.size() >= m_maxHighlights) {
                m_highlightsTruncated = true;
                break;
            }

            // Track which match the editor cursor is inside
            if (cur.selectionStart() <= editorCursor.position() &&
                editorCursor.position() <= cur.selectionEnd())
                currentIndex = count;

            QTextEdit::ExtraSelection sel;
            sel.cursor = cur;
            sel.format.setBackground(hlColor);
            sel.format.setProperty(QTextFormat::FullWidthSelection, false);
            highlights.append(sel);
        }

        currentIndex = qMin(currentIndex, highlights.size());

        // Current-match highlight (brighter)
        if (currentIndex > 0) {
            QColor cur2 = m_theme.tokenKeyword.isValid()
            ? m_theme.tokenKeyword : QColor(255, 165, 0);
            cur2.setAlpha(140);
            highlights[currentIndex - 1].format.setBackground(cur2);
        }

        if (m_highlightsHandler)
            m_highlightsHandler(highlights);
        m_savedHighlights = highlights;
        m_currentMatch    = currentIndex;
        m_totalMatches    = count;

        updateMatchLabel();

        if (count == 0 && !needle.isEmpty())
            m_findEdit->flashError(QColor(200, 60, 60));
    }

    void doFind(bool backward) {
        if (!m_editor) return;
        const QString needle = m_findEdit->text();
        if (needle.isEmpty()) return;

        QTextDocument::FindFlags flags = buildFindFlags(backward);
        QTextCursor cur = m_editor->textCursor();

        // Start from end of selection when going forward, start when backward
        if (!backward) cur.setPosition(cur.selectionEnd());

        QTextCursor found = findIn(m_editor->document(), needle, cur, flags);

        if (found.isNull()) {
            // Wrap around
            QTextCursor wrap(m_editor->document());
            if (backward) {
                wrap.movePosition(QTextCursor::End);
            }
            found = findIn(m_editor->document(), needle, wrap, flags);
        }

        if (!found.isNull()) {
            m_editor->setTextCursor(found);
            m_editor->ensureCursorVisible();

            // Re-run highlight to update current-match index
            doHighlightAll();
        } else {
            m_findEdit->flashError(QColor(200, 60, 60));
        }
    }

    void doReplace() {
        if (!m_editor || m_replaceEdit->isHidden()) return;
        const QString needle  = m_findEdit->text();
        const QString replace = m_replaceEdit->text();
        if (needle.isEmpty()) return;
        clearHighlightSelectionsOnly();
        QScopedValueRollback<bool> replaceGuard(m_replaceInProgress, true);

        QTextCursor cur = m_editor->textCursor();

        // If current selection matches, replace it
        if (cur.hasSelection()) {
            QString sel = cur.selectedText();
            bool matches = false;

            if (m_btnRegex->isActive()) {
                QRegularExpression re = buildRegex(needle);
                QRegularExpressionMatch m = re.match(sel);
                matches = m.hasMatch()
                          && m.capturedStart() == 0
                          && m.capturedLength() == sel.size();
            } else {
                Qt::CaseSensitivity cs = m_btnCase->isActive()
                ? Qt::CaseSensitive
                : Qt::CaseInsensitive;
                matches = (sel.compare(needle, cs) == 0);
            }

            if (matches) {
                QString rep = buildReplacement(needle, replace, cur);
                cur.insertText(rep);
            }
        }

        // Move to next match
        doFind(false);
    }

    void doReplaceAll() {
        if (!m_editor || m_replaceEdit->isHidden()) return;
        const QString needle  = m_findEdit->text();
        const QString replace = m_replaceEdit->text();
        if (needle.isEmpty()) return;
        clearHighlightSelectionsOnly();
        QScopedValueRollback<bool> replaceGuard(m_replaceInProgress, true);

        QTextDocument *doc = m_editor->document();
        QTextDocument::FindFlags flags = buildFindFlags(false);

        struct ReplaceSpan {
            int start = 0;
            int end = 0;
            QString replacement;
        };

        QVector<ReplaceSpan> spans;
        QTextCursor scan(doc);
        const int scanMaxPos = qMax(0, doc->characterCount() - 1);
        int guardPos = -1;
        while (true) {
            scan = findIn(doc, needle, scan, flags);
            if (scan.isNull())
                break;

            const int matchStart = scan.selectionStart();
            const int matchEnd = scan.selectionEnd();
            if (matchEnd <= matchStart) {
                const int nextPos = qMin(matchStart + 1, scanMaxPos);
                if (nextPos <= guardPos)
                    break;
                guardPos = nextPos;
                scan.setPosition(nextPos);
                continue;
            }

            spans.append({matchStart, matchEnd, buildReplacement(needle, replace, scan)});
            guardPos = matchEnd;
            scan.setPosition(matchEnd);
        }

        if (spans.isEmpty()) {
            m_matchLabel->setText("No matches");
            doHighlightAll();
            return;
        }

        const QString source = doc->toPlainText();
        QString replacedText;
        replacedText.reserve(source.size());

        int sourcePos = 0;
        int appliedCount = 0;
        ReplaceSpan firstApplied;
        bool haveFirstApplied = false;
        for (const ReplaceSpan& span : spans) {
            if (span.start < sourcePos || span.end < span.start || span.end > source.size())
                continue;
            replacedText += source.mid(sourcePos, span.start - sourcePos);
            replacedText += span.replacement;
            sourcePos = span.end;
            if (!haveFirstApplied) {
                firstApplied = span;
                haveFirstApplied = true;
            }
            ++appliedCount;
        }
        replacedText += source.mid(sourcePos);

        if (appliedCount == 0) {
            m_matchLabel->setText("No matches");
            doHighlightAll();
            return;
        }

        QTextCursor writer(doc);
        writer.beginEditBlock();
        writer.select(QTextCursor::Document);
        writer.insertText(replacedText);
        writer.endEditBlock();

        const ReplaceSpan first = firstApplied;
        const int maxPos = qMax(0, doc->characterCount() - 1);
        const int selStart = qBound(0, first.start, maxPos);
        const int selEnd = qBound(selStart, first.start + first.replacement.size(), maxPos);
        QTextCursor selection(doc);
        selection.setPosition(selStart);
        selection.setPosition(selEnd, QTextCursor::KeepAnchor);
        m_editor->setTextCursor(selection);

        m_matchLabel->setText(QString("%1 replaced").arg(appliedCount));

        doHighlightAll();
    }

    void onToggleReplace() {
        setReplaceVisible(!m_replaceRow->isVisible());
    }

    void closeBar() {
        clearHighlights();
        m_fadeAnim->stop();
        m_fadeAnim->setStartValue(opacity());
        m_fadeAnim->setEndValue(0.0);
        disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
        connect(m_fadeAnim, &QPropertyAnimation::finished, this, &QWidget::hide);
        m_fadeAnim->start();
        if (m_editor) m_editor->setFocus();
    }

private:
    // ── Build helpers ────────────────────────────────────────────────────────

    void buildWidgets() {
        // Find row
        m_toggleReplace = new FRToolButton("›", this);
        m_toggleReplace->setToolTip("Toggle Replace (Alt+R)");
        m_toggleReplace->setFixedWidth(18);

        m_findEdit = new FRLineEdit("Find", this);
        m_findEdit->setMinimumWidth(180);

        m_btnCase  = makeToggle("Aa", "Match Case (Alt+C)");
        m_btnWord  = makeToggle("W",  "Whole Word (Alt+W)");
        m_btnRegex = makeToggle(".*", "Regular Expression (Alt+E)");

        m_btnPrev  = makePushBtn("˄", "Previous Match (Shift+F3)");
        m_btnNext  = makePushBtn("˅", "Next Match (F3)");
        m_matchLabel = new QLabel(this);
        m_matchLabel->setFixedWidth(80);
        m_matchLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_closeBtn = makePushBtn("✕", "Close (Esc)");
        m_closeBtn->setFixedWidth(22);

        // Replace row
        m_replaceEdit    = new FRLineEdit("Replace", this);
        m_replaceEdit->setMinimumWidth(180);
        m_btnReplace     = makePushBtn("Replace",    "Replace (Enter)");
        m_btnReplaceAll  = makePushBtn("Replace All","Replace All (Ctrl+Alt+Enter)");
    }

    void buildLayout() {
        // ── Find row ─────────────────────────────────────────────────────────
        auto *findRow = new QHBoxLayout;
        findRow->setContentsMargins(0, 0, 0, 0);
        findRow->setSpacing(4);

        findRow->addWidget(m_toggleReplace);
        findRow->addWidget(m_findEdit, 1);
        findRow->addWidget(m_btnCase);
        findRow->addWidget(m_btnWord);
        findRow->addWidget(m_btnRegex);
        findRow->addSpacing(4);
        findRow->addWidget(m_matchLabel);
        findRow->addSpacing(2);
        findRow->addWidget(m_btnPrev);
        findRow->addWidget(m_btnNext);
        findRow->addSpacing(6);
        findRow->addWidget(m_closeBtn);

        // ── Replace row ───────────────────────────────────────────────────────
        m_replaceRow = new QWidget(this);
        auto *replRow = new QHBoxLayout(m_replaceRow);
        replRow->setContentsMargins(22, 0, 0, 0); // indent to align with find field
        replRow->setSpacing(4);
        replRow->addWidget(m_replaceEdit, 1);
        replRow->addWidget(m_btnReplace);
        replRow->addWidget(m_btnReplaceAll);
        replRow->addStretch();

        // ── Main vertical stack ───────────────────────────────────────────────
        auto *vbox = new QVBoxLayout(this);
        vbox->setContentsMargins(16, 8, 16, 8);
        vbox->setSpacing(4);
        vbox->addLayout(findRow);
        vbox->addWidget(m_replaceRow);
        setLayout(vbox);
    }

    void connectSignals() {
        connect(m_findEdit,      &QLineEdit::textChanged,
                this,            &FindReplaceBar::onFindTextChanged);
        connect(m_findEdit,      &QLineEdit::returnPressed,
                this,            &FindReplaceBar::findNext);

        connect(m_btnPrev,       &QToolButton::clicked,
                this,            &FindReplaceBar::findPrevious);
        connect(m_btnNext,       &QToolButton::clicked,
                this,            &FindReplaceBar::findNext);

        connect(m_btnCase,       &QToolButton::clicked,
                this,            [this]{ m_btnCase->setActive(!m_btnCase->isActive());
                                         doHighlightAll(); });
        connect(m_btnWord,       &QToolButton::clicked,
                this,            [this]{ m_btnWord->setActive(!m_btnWord->isActive());
                                         doHighlightAll(); });
        connect(m_btnRegex,      &QToolButton::clicked,
                this,            [this]{ m_btnRegex->setActive(!m_btnRegex->isActive());
                                         doHighlightAll(); });

        connect(m_toggleReplace, &QToolButton::clicked,
                this,            &FindReplaceBar::onToggleReplace);

        connect(m_btnReplace,    &QToolButton::clicked,
                this,            &FindReplaceBar::doReplace);
        connect(m_btnReplaceAll, &QToolButton::clicked,
                this,            &FindReplaceBar::doReplaceAll);
        connect(m_replaceEdit,   &QLineEdit::returnPressed,
                this,            &FindReplaceBar::doReplace);

        connect(m_closeBtn,      &QToolButton::clicked,
                this,            &FindReplaceBar::closeBar);
    }

    FRToolButton *makeToggle(const QString &label, const QString &tip) {
        auto *b = new FRToolButton(label, this);
        b->setToolTip(tip);
        b->setFixedWidth(26);
        return b;
    }

    FRToolButton *makePushBtn(const QString &label, const QString &tip) {
        auto *b = new FRToolButton(label, this);
        b->setToolTip(tip);
        return b;
    }

    void applyTheme() {
        QColor fg      = m_theme.foreground.isValid() ? m_theme.foreground
                                                 : QColor(200,200,200);
        QColor hover   = m_theme.currentLineBackground.isValid()
                           ? m_theme.currentLineBackground : QColor(60,60,60,120);
        QColor checked = m_theme.selectionBackground.isValid()
                             ? m_theme.selectionBackground : QColor(80,80,140,120);

        for (auto *b : { m_btnCase, m_btnWord, m_btnRegex,
                        m_btnPrev, m_btnNext, m_closeBtn,
                        m_toggleReplace, m_btnReplace, m_btnReplaceAll })
            b->setTheme(fg, hover, checked);

        m_findEdit->setTheme(m_theme);
        m_replaceEdit->setTheme(m_theme);

        // Match counter label color
        QString labelColor = m_theme.tokenComment.isValid()
                                 ? m_theme.tokenComment.name()
                                 : "#888";
        m_matchLabel->setStyleSheet(
            QString("color: %1; font-size: %2px;")
                .arg(labelColor)
                .arg(qMax(10, m_theme.fontSize - 2)));
    }

    void setReplaceVisible(bool visible) {
        m_replaceRow->setVisible(visible);
        m_toggleReplace->setText(visible ? "˅" : "›");
        adjustSize();
        repositionToParent();
    }

    void openBar() {
        if (m_editor) {
            QString sel = m_editor->textCursor().selectedText();
            // Don't pre-fill multiline selections
            if (!sel.contains('\n') && !sel.isEmpty())
                m_findEdit->setText(sel);
        }

        repositionToParent();

        m_fadeAnim->stop();
        m_fadeAnim->setStartValue(0.0);
        m_fadeAnim->setEndValue(1.0);
        disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
        show();
        raise();
        m_fadeAnim->start();

        m_findEdit->setFocus();
        m_findEdit->selectAll();
        doHighlightAll();
    }

    void repositionToParent() {
        if (!parentWidget()) return;
        const int marginX = 10;
        const int marginY = 10;
        const int maxWidth = 600;

        const int pw = qMax(0, parentWidget()->width());
        const int ph = qMax(0, parentWidget()->height());
        const int availableW = qMax(1, pw - (marginX * 2));
        const int bw = qMin(maxWidth, availableW);
        const int bh = qMax(minimumSizeHint().height(), sizeHint().height());

        const int xMax = qMax(0, pw - bw);
        const int yMax = qMax(0, ph - bh);
        const int x = qBound(0, pw - bw - marginX, xMax);
        const int y = qBound(0, marginY, yMax);

        setGeometry(x, y, bw, bh);
    }

    void clearHighlights() {
        clearHighlightSelectionsOnly();
        m_totalMatches = 0;
        m_currentMatch = 0;
        m_highlightsTruncated = false;
    }

    void clearHighlightSelectionsOnly() {
        if (m_highlightsHandler)
            m_highlightsHandler({});
        m_savedHighlights.clear();
    }

    void updateMatchLabel() {
        if (m_totalMatches == 0)
            m_matchLabel->setText(m_findEdit->text().isEmpty() ? "" : "No results");
        else
            m_matchLabel->setText(QString("%1 / %2")
                                      .arg(m_currentMatch)
                                      .arg(m_highlightsTruncated
                                               ? QString("%1+").arg(qMax(1, m_totalMatches - 1))
                                               : QString::number(m_totalMatches)));
    }

    // ── Core find logic (mirrors QFindDialogs internals) ─────────────────────

    QTextDocument::FindFlags buildFindFlags(bool backward) const {
        QTextDocument::FindFlags f;
        if (backward)           f |= QTextDocument::FindBackward;
        if (m_btnCase->isActive()) f |= QTextDocument::FindCaseSensitively;
        if (m_btnWord->isActive()) f |= QTextDocument::FindWholeWords;
        return f;
    }

    QRegularExpression buildRegex(const QString &pattern) const {
        QRegularExpression::PatternOptions opts =
            QRegularExpression::NoPatternOption;
        if (!m_btnCase->isActive())
            opts |= QRegularExpression::CaseInsensitiveOption;
        return QRegularExpression(pattern, opts);
    }

    // Unified find: uses regex path if regex-mode is on, plain text otherwise.
    QTextCursor findIn(QTextDocument *doc, const QString &needle,
                       QTextCursor from,
                       QTextDocument::FindFlags flags) const
    {
        if (m_btnRegex->isActive()) {
            // QTextDocument::find(QRegularExpression) ignores FindCaseSensitively
            // flag but we bake it into the regex options.
            QRegularExpression re = buildRegex(needle);
            if (!re.isValid()) return {};

            // Regex doesn't support FindBackward directly in Qt < 6.1 —
            // simulate it by collecting all matches and picking the last one
            // before the cursor.
            if (flags & QTextDocument::FindBackward) {
                int limit = from.position();
                QTextCursor best;
                QTextCursor c(doc);
                while (true) {
                    c = doc->find(re, c);
                    if (c.isNull() || c.selectionStart() >= limit) break;
                    best = c;
                }
                return best;
            }
            return doc->find(re, from);
        }
        return doc->find(needle, from, flags);
    }

    // Build replacement string — expands \1 capture groups when in regex mode
    QString buildReplacement(const QString &needle, const QString &replace,
                             const QTextCursor &matchCursor) const
    {
        if (!m_btnRegex->isActive()) return replace;

        QRegularExpression re = buildRegex(needle);
        QString selected = matchCursor.selectedText();
        QRegularExpressionMatch m = re.match(selected);
        if (!m.hasMatch()) return replace;

        // Replace \1..\9 back-references
        QString result = replace;
        for (int i = m.lastCapturedIndex(); i >= 1; --i)
            result.replace(QString("\\%1").arg(i), m.captured(i));
        result.replace("\\0", m.captured(0));
        return result;
    }

    // ── Members ───────────────────────────────────────────────────────────────
    QPlainTextEdit *m_editor = nullptr;
    QEditorTheme    m_theme;

    // Find row
    FRToolButton *m_toggleReplace = nullptr;
    FRLineEdit   *m_findEdit      = nullptr;
    FRToolButton *m_btnCase       = nullptr;
    FRToolButton *m_btnWord       = nullptr;
    FRToolButton *m_btnRegex      = nullptr;
    FRToolButton *m_btnPrev       = nullptr;
    FRToolButton *m_btnNext       = nullptr;
    QLabel       *m_matchLabel    = nullptr;
    FRToolButton *m_closeBtn      = nullptr;

    // Replace row
    QWidget      *m_replaceRow    = nullptr;
    FRLineEdit   *m_replaceEdit   = nullptr;
    FRToolButton *m_btnReplace    = nullptr;
    FRToolButton *m_btnReplaceAll = nullptr;

    // Animation
    QGraphicsOpacityEffect *m_opacity  = nullptr;
    QPropertyAnimation     *m_fadeAnim = nullptr;
    QTimer                 *m_searchTimer = nullptr;

    // Match tracking
    QList<QTextEdit::ExtraSelection> m_savedHighlights;
    int m_currentMatch = 0;
    int m_totalMatches = 0;
    int m_maxHighlights = 0;
    bool m_highlightsTruncated = false;
    std::function<void(const QList<QTextEdit::ExtraSelection>&)> m_highlightsHandler;
    QMetaObject::Connection m_docChangeConnection;
    bool m_replaceInProgress = false;
};

#endif // FINDREPLACEBAR_H
