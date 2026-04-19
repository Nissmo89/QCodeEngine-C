#include "CodeEditor/CodeEditor.h"
#include "CodeEditor_p.h"
#include <QHBoxLayout>
#include <QFile>
#include <QTextBlock>
#include <QPainter>
#include <QVector>
#include <QTimer>
#include "TreeSitterQuery_C.h"

extern "C" const TSLanguage *tree_sitter_c(void);

// ── Bracket matching ─────────────────────────────────────────────────────────

namespace {

struct CLexer {
    enum Phase { Normal, LineComment, BlockComment, String, Char } phase = Normal;
    bool esc = false;

    bool codeForBrackets() const { return phase == Normal; }

    void push(const QString& s, int i) {
        QChar c = s.at(i);
        switch (phase) {
        case LineComment:
            if (c == QLatin1Char('\n') || c == QChar::ParagraphSeparator || c == QChar::LineSeparator)
                phase = Normal;
            return;
        case BlockComment:
            if (c == QLatin1Char('*') && i + 1 < s.size() && s.at(i + 1) == QLatin1Char('/'))
                phase = Normal;
            return;
        case String:
            if (esc) { esc = false; return; }
            if (c == QLatin1Char('\\')) { esc = true; return; }
            if (c == QLatin1Char('"')) phase = Normal;
            return;
        case Char:
            if (esc) { esc = false; return; }
            if (c == QLatin1Char('\\')) { esc = true; return; }
            if (c == QLatin1Char('\'')) phase = Normal;
            return;
        case Normal:
            if (c == QLatin1Char('/') && i + 1 < s.size()) {
                QChar n = s.at(i + 1);
                if (n == QLatin1Char('/')) { phase = LineComment; return; }
                if (n == QLatin1Char('*')) { phase = BlockComment; return; }
            }
            if (c == QLatin1Char('"'))  { phase = String; esc = false; return; }
            if (c == QLatin1Char('\'')) { phase = Char;   esc = false; return; }
            return;
        }
    }
};

static void buildBracketCountableMask(const QString& s, QVector<bool>& mask) {
    const int n = s.size();
    mask.resize(n);
    CLexer lx;
    for (int i = 0; i < n; ++i) { mask[i] = lx.codeForBrackets(); lx.push(s, i); }
}

static bool isBracketChar (QChar c) {
    return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
}
static bool isOpenBracket (QChar c) { return c == '(' || c == '[' || c == '{'; }
static bool isCloseBracket(QChar c) { return c == ')' || c == ']' || c == '}'; }
static QChar closingFor(QChar o) {
    if (o == '(') return ')'; if (o == '[') return ']'; if (o == '{') return '}';
    return QChar();
}

static int findClosingPartner(const QString& s, const QVector<bool>& mask, int openPos) {
    if (!isOpenBracket(s.at(openPos))) return -1;
    QVector<QChar> stack; stack.push_back(closingFor(s.at(openPos)));
    for (int i = openPos + 1; i < s.size(); ++i) {
        if (!mask.at(i)) continue;
        QChar c = s.at(i);
        if (isOpenBracket(c))  { stack.push_back(closingFor(c)); }
        else if (isCloseBracket(c)) {
            if (stack.isEmpty() || c != stack.last()) return -1;
            stack.pop_back();
            if (stack.isEmpty()) return i;
        }
    }
    return -1;
}

static int findOpeningPartner(const QString& s, const QVector<bool>& mask, int closePos) {
    if (!isCloseBracket(s.at(closePos))) return -1;
    QVector<QChar> stack; stack.push_back(s.at(closePos));
    for (int i = closePos - 1; i >= 0; --i) {
        if (!mask.at(i)) continue;
        QChar c = s.at(i);
        if (isOpenBracket(c)) {
            if (stack.isEmpty() || closingFor(c) != stack.last()) return -1;
            stack.pop_back();
            if (stack.isEmpty()) return i;
        } else if (isCloseBracket(c)) { stack.push_back(c); }
    }
    return -1;
}

static int bracketIndexAtCursor(const QString& s, int cursorPos) {
    const int n = s.size();
    if (n == 0) return -1;
    if (cursorPos >= 0 && cursorPos < n   && isBracketChar(s.at(cursorPos)))   return cursorPos;
    if (cursorPos > 0  && cursorPos-1 < n && isBracketChar(s.at(cursorPos-1))) return cursorPos - 1;
    return -1;
}

} // namespace

// ── Format map ───────────────────────────────────────────────────────────────

static FormatMap generateFormatMap(const QEditorTheme& theme) {
    FormatMap fmap;
    auto makeFormat = [](QColor color, bool bold = false, bool italic = false) {
        QTextCharFormat fmt;
        fmt.setForeground(color);
        if (bold)   fmt.setFontWeight(QFont::Bold);
        if (italic) fmt.setFontItalic(true);
        return fmt;
    };
    fmap["keyword"]               = makeFormat(theme.tokenKeyword, theme.keywordBold);
    fmap["keyword.control"]       = makeFormat(theme.tokenKeywordControl, theme.keywordBold);
    fmap["keyword.preproc"]       = makeFormat(theme.tokenKeywordPreproc, theme.keywordBold);
    fmap["preproc"]               = makeFormat(theme.tokenKeywordPreproc);
    fmap["preproc.arg"]           = makeFormat(theme.tokenPreprocessor);
    fmap["operator"]              = makeFormat(theme.tokenOperator);
    fmap["punctuation.delimiter"] = makeFormat(theme.tokenPunctuation);
    fmap["punctuation.bracket"]   = makeFormat(theme.tokenPunctuation);
    fmap["punctuation"]           = makeFormat(theme.tokenPunctuation);
    fmap["string"]                = makeFormat(theme.tokenString);
    fmap["string.escape"]         = makeFormat(theme.tokenEscape);
    fmap["number"]                = makeFormat(theme.tokenNumber);
    fmap["boolean"]               = makeFormat(theme.tokenBoolean);
    fmap["constant.builtin"]      = makeFormat(theme.tokenConstantBuiltin);
    fmap["constant"]              = makeFormat(theme.tokenConstant);
    fmap["comment"]               = makeFormat(theme.tokenComment, false, theme.commentItalic);
    fmap["variable"]              = makeFormat(theme.tokenIdentifier);
    fmap["function"]              = makeFormat(theme.tokenFunction, theme.functionBold);
    fmap["function.special"]      = makeFormat(theme.tokenKeywordPreproc, theme.functionBold);
    fmap["type"]                  = makeFormat(theme.tokenType, theme.typeBold);
    fmap["property"]              = makeFormat(theme.tokenField);
    fmap["label"]                 = makeFormat(theme.tokenLabel);
    fmap["attribute"]             = makeFormat(theme.tokenAttribute);
    { QTextCharFormat fb; fb.setForeground(theme.foreground); fmap[""] = fb; }
    return fmap;
}

static void applyEditorStyle(QPlainTextEdit* editor, int lineHeightPx = 26) {
    QTextBlockFormat fmt;
    fmt.setLineHeight(lineHeightPx, QTextBlockFormat::FixedHeight);
    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.setBlockFormat(fmt);
    cursor.clearSelection();
    cursor.endEditBlock();
}

// ── InnerEditor ──────────────────────────────────────────────────────────────

InnerEditor::InnerEditor(CodeEditorPrivate* d, QWidget* parent)
    : QPlainTextEdit(parent), d_ptr(d) {}

void InnerEditor::keyPressEvent(QKeyEvent* e) {
    if (d_ptr->m_completer && d_ptr->m_completer->handleKeyPress(e)) return;
    if (d_ptr->handleKeyPress(e)) return;
    QPlainTextEdit::keyPressEvent(e);
    if (d_ptr->m_completer && !e->text().isEmpty())
        d_ptr->m_completer->updatePopup();
}

void InnerEditor::paintEvent(QPaintEvent* e) {
    // ── Preserve syntax colors under selection ────────────────────────────────
    //
    // Qt's QTextDocumentLayout replaces every character's foreground with
    // QPalette::HighlightedText for selected ranges — wiping out all syntax
    // highlight colors.  The fix:
    //
    //   1. Temporarily clear the cursor selection before the base paint.
    //      Qt now renders all text with their real QTextCharFormat colors.
    //   2. Restore the real cursor (no repaint triggered — we're already inside
    //      paintEvent, so no recursive call occurs).
    //   3. Manually paint a semi-transparent selection rectangle on top as a
    //      QPainter overlay — gives the blue selection wash without clobbering
    //      any foreground color.
    //
    // This is the same technique used by Qt Creator and Kate.

    const QTextCursor savedCursor = textCursor();

    if (savedCursor.hasSelection()) {
        // Step 1: paint text without selection (full syntax colors preserved)
        QTextCursor blank = savedCursor;
        blank.clearSelection();
        setTextCursor(blank);           // does NOT repaint inside paintEvent
        QPlainTextEdit::paintEvent(e);
        setTextCursor(savedCursor);     // restore — also no repaint here

        // Step 3: draw selection background as semi-transparent overlay
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing, false);

        // Selection color: use the palette highlight but force a comfortable alpha
        QColor selColor = palette().color(QPalette::Highlight);
        selColor.setAlpha(100);         // ~40 % — adjust to taste
        painter.setBrush(selColor);
        painter.setPen(Qt::NoPen);

        const int selStart = savedCursor.selectionStart();
        const int selEnd   = savedCursor.selectionEnd();

        // Walk visible blocks and shade any that overlap the selection
        QTextBlock block = firstVisibleBlock();
        qreal top    = blockBoundingGeometry(block).translated(contentOffset()).top();
        qreal bottom = top + blockBoundingRect(block).height();
        const int vpWidth = viewport()->width();

        while (block.isValid() && top <= e->rect().bottom()) {
            if (block.isVisible() && bottom >= e->rect().top()) {
                const int blockStart = block.position();
                const int blockEnd   = blockStart + block.length() - 1; // excl. \n

                // Does this block overlap [selStart, selEnd)?
                if (blockStart <= selEnd && blockEnd >= selStart) {
                    const int overlapStart = qMax(selStart, blockStart);
                    const int overlapEnd   = qMin(selEnd,   blockEnd);

                    if (overlapStart == blockStart && overlapEnd == blockEnd) {
                        // Whole line selected — full-width rect
                        painter.drawRect(QRectF(0, top, vpWidth, bottom - top));
                    } else {
                        // Partial line — measure character positions
                        QTextCursor c1(document());
                        c1.setPosition(overlapStart);
                        const QRect r1 = cursorRect(c1);

                        QTextCursor c2(document());
                        c2.setPosition(overlapEnd);
                        const QRect r2 = cursorRect(c2);

                        painter.drawRect(QRectF(r1.left(), top,
                                                r2.right() - r1.left(),
                                                bottom - top));
                    }
                }
            }
            block  = block.next();
            top    = bottom;
            bottom = top + blockBoundingRect(block).height();
        }
    } else {
        QPlainTextEdit::paintEvent(e);  // no selection — normal path, no overhead
    }

    // Draw " ...}" hint on collapsed fold header lines
    if (!d_ptr->m_foldingEnabled) return;

    QPainter painter(viewport());
    painter.setFont(font());
    QFontMetrics fm(font());

    QTextBlock block = firstVisibleBlock();
    int  blockNumber = block.blockNumber();
    qreal top    = blockBoundingGeometry(block).translated(contentOffset()).top();
    qreal bottom = top + blockBoundingRect(block).height();

    while (block.isValid() && top <= e->rect().bottom()) {
        if (block.isVisible() && bottom >= e->rect().top()) {
            if (d_ptr->m_foldManager->isFolded(blockNumber)) {
                // Measure the existing line text to place hint after it
                const int textW = fm.horizontalAdvance(
                    block.text().replace('\t', QString(d_ptr->m_tabWidth, ' ')));
                const int blockH = static_cast<int>(bottom - top);

                painter.save();
                painter.setPen(d_ptr->m_theme.tokenComment);
                painter.drawText(textW + 8, static_cast<int>(top),
                                 fm.horizontalAdvance(" ...}"), blockH,
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 " ...}");
                painter.restore();
            }
        }
        block = block.next();
        top   = bottom;
        bottom = top + blockBoundingRect(block).height();
        ++blockNumber;
    }
}

// ── CodeEditorPrivate constructor ─────────────────────────────────────────────

CodeEditorPrivate::CodeEditorPrivate(CodeEditor* q, QWidget* parent)
    : QObject(parent), q_ptr(q), m_editor(new InnerEditor(this))
{
    m_gutter      = new GutterWidget(m_editor, q);
    m_foldManager = new FoldManager(this);
    m_foldManager->setDocument(m_editor->document());

    QHBoxLayout* layout = new QHBoxLayout(q);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_gutter);
    layout->addWidget(m_editor);

    // Keep line-height format consistent after content changes
    connect(m_editor->document(), &QTextDocument::contentsChange,
            m_editor, [this](int from, int /*charsRemoved*/, int charsAdded) {
                if (charsAdded <= 0) return;
                QTextBlock b   = m_editor->document()->findBlock(from);
                QTextBlock end = m_editor->document()->findBlock(from + charsAdded);
                QTextBlockFormat fmt;
                fmt.setLineHeight(26, QTextBlockFormat::FixedHeight);
                QTextCursor cur(m_editor->document());
                cur.beginEditBlock();
                while (b.isValid()) {
                    if (b.blockFormat().lineHeightType() != QTextBlockFormat::FixedHeight) {
                        cur.setPosition(b.position());
                        cur.setBlockFormat(fmt);
                    }
                    if (b == end) break;
                    b = b.next();
                }
                cur.endEditBlock();
            });

    // ── Highlighter setup ────────────────────────────────────────────────────
    m_highlighter = new TreeSitterHighlighter(
        tree_sitter_c(), std::string(HIGHLIGHTS_SCM),
        generateFormatMap(m_theme), m_editor->document());
    m_highlighter->set_rainbow_colors(m_theme.rainbowColors);

    // ── FoldManager wired to highlighter "parsed" signal ────────────────────
    // KEY DESIGN: the highlighter already ran a full incremental tree-sitter
    // parse.  We hand the finished TSTree* to FoldManager so it can run fold
    // queries on it — zero additional parsing cost.
    connect(m_highlighter, &TreeSitterHighlighter::parsed,
            this, [this](void* treePtr) {
                if (m_foldingEnabled)
                    m_foldManager->updateFoldRanges(treePtr, m_editor->document());
            });

    // ── LineHighlighter — notebook-style {N,#COLOR} comment tags ────────────
    m_lineHighlighter = new LineHighlighter(this);
    m_lineHighlighter->setDocument(m_editor->document());
    m_lineHighlighter->setEditor(m_editor);
    // Borrows the same TSTree* as FoldManager — zero extra parse cost.
    connect(m_highlighter, &TreeSitterHighlighter::parsed,
            this, [this](void* treePtr) {
                m_lineHighlighter->updateFromTree(treePtr, m_editor->document());
            });
    // When the highlight map changes, rebuild the merged extra-selection list.
    connect(m_lineHighlighter, &LineHighlighter::highlightChanged,
            this, [this]() {
                m_lineHighlightSelections = m_lineHighlighter->extraSelections();
                updateCurrentLineHighlight();
            });

    // After fold ranges are rebuilt, refresh the gutter arrows
    connect(m_foldManager, &FoldManager::foldRangesUpdated,
            this, &CodeEditorPrivate::updateGutterFoldRanges);

    // After a toggle (collapse/expand), refresh gutter and repaint viewport
    connect(m_foldManager, &FoldManager::foldStateChanged, this, [this]() {
        updateGutterFoldRanges();
        m_editor->viewport()->update();
    });

    // ── Auto-completer ───────────────────────────────────────────────────────
    m_completer = new AutoCompleter(this);
    m_completer->setEditor(m_editor);

    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);

    connect(m_editor, &QPlainTextEdit::blockCountChanged,
            this, &CodeEditorPrivate::updateLineNumberAreaWidth);
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged,
            this, &CodeEditorPrivate::onCursorPositionChanged);
    connect(m_editor, &QPlainTextEdit::textChanged,
            this, &CodeEditorPrivate::onTextChanged);

    connect(m_gutter, &GutterWidget::foldToggled,
            this, &CodeEditorPrivate::onGutterFoldClicked);
    connect(m_gutter, &GutterWidget::markerToggled,
            this, [this](int line, MarkerType type) {
                GutterIconType iconType = GutterIconType::Info;
                if (type == MarkerType::Error)      iconType = GutterIconType::Error;
                else if (type == MarkerType::Warning)    iconType = GutterIconType::Warning;
                else if (type == MarkerType::Breakpoint) iconType = GutterIconType::Breakpoint;
                emit q_ptr->gutterIconClicked(line, iconType);
            });
    connect(m_editor->document(), &QTextDocument::modificationChanged,
            this, [this](bool modified) {
                emit q_ptr->documentModifiedChanged(modified);
            });

    applyEditorStyle(m_editor);
    updateLineNumberAreaWidth(0);

    // ── Function list popup ──────────────────────────────────────────────────
    m_functionPopup = new FloatingListPopup(q);
    connect(m_functionPopup, &FloatingListPopup::functionSelected,
            this, &CodeEditorPrivate::onFunctionSelected);

    // Debounced: updateFunctionList does a full parse; don't fire on every keystroke
    m_functionListTimer = new QTimer(this);
    m_functionListTimer->setSingleShot(true);
    m_functionListTimer->setInterval(500);
    connect(m_editor->document(), &QTextDocument::contentsChanged,
            this, [this]() { m_functionListTimer->start(); });
    connect(m_functionListTimer, &QTimer::timeout,
            this, &CodeEditorPrivate::updateFunctionList);

    QAction* showFunctionsAction = new QAction(q);
    showFunctionsAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(showFunctionsAction, &QAction::triggered, q, &CodeEditor::showFunctionList);
    q->addAction(showFunctionsAction);

    QTimer::singleShot(0, this, [this]() {
        m_gutter->updateWidth();
        m_gutter->update();
    });
}

// ── Private helpers ───────────────────────────────────────────────────────────

void CodeEditorPrivate::updateLineNumberAreaWidth(int) {
    m_gutter->updateWidth();
}

void CodeEditorPrivate::updateLineNumberArea(const QRect& rect, int dy) {
    m_gutter->syncScrollWith(rect, dy);
    if (rect.contains(m_editor->viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditorPrivate::updateGutterFoldRanges()
{
    // Convert 0-based FoldManager ranges to 1-based FoldArea::FoldRange list
    const QMap<int,int>& foldMap = m_foldManager->foldRanges();
    QList<FoldArea::FoldRange> ranges;
    ranges.reserve(foldMap.size());
    for (auto it = foldMap.begin(); it != foldMap.end(); ++it) {
        ranges.append({ it.key() + 1,               // startLine (1-based)
                       it.value() + 1,              // endLine   (1-based)
                       m_foldManager->isFolded(it.key()) });
    }
    m_gutter->setFoldRanges(ranges);
    m_gutter->update();
}

void CodeEditorPrivate::setFoldingEnabled(bool enabled)
{
    m_foldingEnabled = enabled;
    if (!enabled) {
        // Unhide all blocks when folding is turned off
        QTextDocument* doc = m_editor->document();
        QTextBlock block = doc->begin();
        while (block.isValid()) {
            if (!block.isVisible()) {
                block.setVisible(true);
                block.setLineCount(1);
            }
            block = block.next();
        }
        doc->markContentsDirty(0, doc->characterCount());
        m_editor->viewport()->update();
        m_foldManager->unfoldAll();
    }
}

void CodeEditorPrivate::updateCurrentLineHighlight() {
    QList<QTextEdit::ExtraSelection> extras;
    if (!m_editor->isReadOnly()) {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(m_theme.currentLineBackground);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = m_editor->textCursor();
        sel.cursor.clearSelection();
        extras.append(sel);
    }
    // Line-highlight selections drawn first (lowest z-order) so bracket
    // and search highlights paint on top of them.
    extras.append(m_lineHighlightSelections);
    extras.append(m_bracketSelections);
    extras.append(m_searchSelections);
    m_editor->setExtraSelections(extras);
}

void CodeEditorPrivate::updateBracketMatch() {
    m_bracketSelections.clear();
    QTextDocument* doc = m_editor->document();
    const QString text = doc->toPlainText();
    const int cursorPos = m_editor->textCursor().position();
    const int idx = bracketIndexAtCursor(text, cursorPos);
    if (idx < 0) return;

    QVector<bool> mask;
    buildBracketCountableMask(text, mask);
    if (!mask.at(idx)) return;

    auto makeSel = [doc, this](int from, int len, bool mismatch) {
        QTextEdit::ExtraSelection es;
        if (mismatch) {
            es.format.setBackground(m_theme.bracketMismatchBackground.isValid()
                                    ? m_theme.bracketMismatchBackground : QColor(180, 60, 60, 90));
        } else {
            es.format.setBackground(m_theme.bracketMatchBackground);
        }
        es.cursor = QTextCursor(doc);
        es.cursor.setPosition(from);
        es.cursor.setPosition(from + len, QTextCursor::KeepAnchor);
        return es;
    };

    const QChar ch = text.at(idx);
    if (isOpenBracket(ch)) {
        int partner = findClosingPartner(text, mask, idx);
        m_bracketSelections.append(makeSel(idx, 1, partner < 0));
        if (partner >= 0) m_bracketSelections.append(makeSel(partner, 1, false));
    } else if (isCloseBracket(ch)) {
        int partner = findOpeningPartner(text, mask, idx);
        m_bracketSelections.append(makeSel(idx, 1, partner < 0));
        if (partner >= 0) m_bracketSelections.append(makeSel(partner, 1, false));
    }
}

bool CodeEditorPrivate::handleKeyPress(QKeyEvent* event) {
    if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_T) {
        static int themeIndex = 0;
        static const std::function<QEditorTheme()> themes[] = {
            QEditorTheme::oneDarkTheme, QEditorTheme::draculaTheme,
            QEditorTheme::monokaiTheme, QEditorTheme::solarizedDarkTheme,
            QEditorTheme::githubLightTheme, QEditorTheme::cursorDarkTheme
        };
        themeIndex = (themeIndex + 1) % (int)(sizeof(themes)/sizeof(themes[0]));
        q_ptr->setTheme(themes[themeIndex]());
        return true;
    }
    if (event->key() == Qt::Key_Tab && m_editor->textCursor().hasSelection()) {
        indentSelection(true); return true;
    }
    if (event->key() == Qt::Key_Backtab) { indentSelection(false); return true; }
    if (event->key() == Qt::Key_Slash && (event->modifiers() & Qt::ControlModifier)) {
        toggleLineComment(); return true;
    }
    if (m_autoIndent && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        QTextCursor cursor = m_editor->textCursor();
        QString currentLine = cursor.block().text();
        int spaces = 0;
        for (QChar ch : currentLine) {
            if (ch == ' ') spaces++; else if (ch == '\t') spaces += m_tabWidth; else break;
        }
        bool openBrace = currentLine.trimmed().endsWith('{');
        cursor.insertText("\n");
        QString indent = m_insertSpaces
                             ? QString(spaces + (openBrace ? m_tabWidth : 0), ' ')
                             : QString(spaces / m_tabWidth + (openBrace ? 1 : 0), '\t');
        cursor.insertText(indent);
        m_editor->setTextCursor(cursor);
        return true;
    }
    if (m_autoBracket) {
        QChar typed = event->text().isEmpty() ? QChar() : event->text()[0];
        static const QMap<QChar,QChar> pairs = {
            {'(',')'}, {'[',']'}, {'{','}'}, {'"','"'}, {'\'','\''}
        };
        static const QSet<QChar> closers = {')', ']', '}', '"', '\''};
        if (closers.contains(typed)) {
            QTextCursor cursor = m_editor->textCursor();
            if (!cursor.hasSelection()) {
                QTextBlock blk = cursor.block();
                int col = cursor.positionInBlock();
                if (col < blk.length() - 1 && blk.text().at(col) == typed) {
                    cursor.movePosition(QTextCursor::Right);
                    m_editor->setTextCursor(cursor);
                    return true;
                }
            }
        }
        if (pairs.contains(typed)) {
            QTextCursor cursor = m_editor->textCursor();
            cursor.beginEditBlock();
            cursor.insertText(event->text() + pairs[typed]);
            cursor.movePosition(QTextCursor::Left);
            cursor.endEditBlock();
            m_editor->setTextCursor(cursor);
            return true;
        }
    }
    return false;
}

void CodeEditorPrivate::indentSelection(bool indent) {
    QTextCursor cursor = m_editor->textCursor();
    int start = cursor.selectionStart(), end = cursor.selectionEnd();
    QTextBlock block = m_editor->document()->findBlock(start);
    int endBlockNum  = m_editor->document()->findBlock(qMax(0, end-(cursor.hasSelection()?1:0))).blockNumber();
    cursor.beginEditBlock();
    while (block.isValid() && block.blockNumber() <= endBlockNum) {
        QTextCursor bc(block);
        if (indent) {
            bc.movePosition(QTextCursor::StartOfBlock);
            bc.insertText(m_insertSpaces ? QString(m_tabWidth, ' ') : "\t");
        } else {
            QString text = block.text(); int toRemove = 0;
            for (int i = 0; i < qMin(m_tabWidth, text.size()); ++i) {
                if (text[i]==' ') toRemove++; else if(text[i]=='\t'){toRemove=1;break;} else break;
            }
            bc.movePosition(QTextCursor::StartOfBlock);
            bc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, toRemove);
            bc.removeSelectedText();
        }
        block = block.next();
    }
    cursor.endEditBlock();
}

void CodeEditorPrivate::toggleLineComment() {
    QTextCursor cursor = m_editor->textCursor();
    int start = cursor.selectionStart(), end = cursor.selectionEnd();
    QTextBlock block = m_editor->document()->findBlock(start);
    int endBlockNum  = m_editor->document()->findBlock(qMax(0,end-(cursor.hasSelection()?1:0))).blockNumber();
    cursor.beginEditBlock();
    while (block.isValid() && block.blockNumber() <= endBlockNum) {
        QTextCursor bc(block);
        QString lineText = block.text(), trimmed = lineText.trimmed();
        if (trimmed.startsWith("//")) {
            int pos = lineText.indexOf("//");
            bc.setPosition(block.position() + pos);
            bc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
            bc.removeSelectedText();
        } else {
            int indent = 0;
            while (indent < lineText.size() && (lineText.at(indent)==' '||lineText.at(indent)=='\t'))
                ++indent;
            bc.setPosition(block.position() + indent);
            bc.insertText("//");
        }
        block = block.next();
    }
    cursor.endEditBlock();
}

void CodeEditorPrivate::onCursorPositionChanged() {
    // ── Auto-unfold guard ────────────────────────────────────────────────────
    // If the cursor lands on a hidden block, find the innermost collapsed fold
    // containing it and open that fold.
    // Boundary: startRow < line < endRow (exclusive, closing brace stays visible)
    {
        QTextBlock curBlock = m_editor->textCursor().block();
        if (!curBlock.isVisible()) {
            int foldStart = m_foldManager->findFoldContaining(curBlock.blockNumber());
            if (foldStart >= 0) {
                m_foldManager->toggleFold(foldStart);
                // foldStateChanged signal will trigger updateGutterFoldRanges + viewport update
            }
        }
    }
    updateBracketMatch();
    updateCurrentLineHighlight();
    QTextCursor cur = m_editor->textCursor();
    int blockNum = cur.blockNumber();
    m_gutter->setCurrentLine(blockNum + 1);
    emit q_ptr->cursorPositionChanged(blockNum + 1, cur.columnNumber() + 1);
    if (cur.hasSelection()) {
        QTextCursor s = cur; s.setPosition(cur.selectionStart());
        QTextCursor e = cur; e.setPosition(cur.selectionEnd());
        emit q_ptr->selectionChanged(
            s.blockNumber()+1, s.columnNumber()+1,
            e.blockNumber()+1, e.columnNumber()+1);
    }
}

void CodeEditorPrivate::onTextChanged() { emit q_ptr->textChanged(); }

void CodeEditorPrivate::onGutterFoldClicked(int line, bool /*folded*/)
{
    // GutterWidget uses 1-based lines; FoldManager uses 0-based
    m_foldManager->toggleFold(qMax(0, line - 1));
    // foldStateChanged signal → updateGutterFoldRanges + viewport update
}

void CodeEditorPrivate::updateFunctionList()
{
    TreeSitterHelper helper(m_editor->toPlainText());
    m_functionPopup->clear();
    for (const auto& func : helper.functions)
        m_functionPopup->addFunction(func.signature, func.startLine + 1);
}

void CodeEditorPrivate::onFunctionSelected(int line)
{
    q_ptr->goToLine(line);
    emit q_ptr->functionSelected(line);
}

// ── CodeEditor Public API ─────────────────────────────────────────────────────

CodeEditor::CodeEditor(QWidget* parent)
    : QWidget(parent), d_ptr(new CodeEditorPrivate(this, this))
{
    d_ptr->updateCurrentLineHighlight();
}

CodeEditor::~CodeEditor() = default;

void CodeEditor::setText(const QString& text) {
    d_ptr->m_editor->setPlainText(text);
    applyEditorStyle(d_ptr->m_editor);
    if (d_ptr->m_highlighter) d_ptr->m_highlighter->rehighlight();
    d_ptr->m_gutter->updateWidth();
    d_ptr->m_gutter->update();
    QTimer::singleShot(0, this, [this]() {
        d_ptr->m_gutter->updateWidth();
        d_ptr->m_gutter->update();
    });
}

QString CodeEditor::text() const { return d_ptr->m_editor->toPlainText(); }

void CodeEditor::insertText(const QString& text) {
    QTextCursor tc = d_ptr->m_editor->textCursor();
    tc.insertText(text);
    d_ptr->m_editor->setTextCursor(tc);
}

void CodeEditor::clear() { d_ptr->m_editor->clear(); }

bool CodeEditor::loadFile(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    setText(QString::fromUtf8(f.readAll()));
    emit fileLoaded(filePath);
    return true;
}

bool CodeEditor::saveFile(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(text().toUtf8());
    emit fileSaved(filePath);
    return true;
}

void CodeEditor::setTheme(const QEditorTheme& theme) {
    Q_D(CodeEditor);
    d->m_theme = theme;
    QFont editorFont(theme.fontFamily, theme.fontSize);
    editorFont.setFixedPitch(true);
    editorFont.setStyleHint(QFont::Monospace);
    editorFont.setHintingPreference(QFont::PreferFullHinting);
    editorFont.setLetterSpacing(QFont::PercentageSpacing, 100);
    d->m_editor->setFont(editorFont);
    d->m_editor->document()->setDocumentMargin(6);
    QPalette pal = d->m_editor->palette();
    pal.setColor(QPalette::Base,             theme.background);
    pal.setColor(QPalette::Text,             theme.foreground);
    pal.setColor(QPalette::Highlight,        theme.selectionBackground);
    pal.setColor(QPalette::HighlightedText,  theme.selectionForeground);
    d->m_editor->setPalette(pal);
    d->m_gutter->setTheme(theme);
    if (d->m_highlighter) {
        d->m_highlighter->set_format_map(generateFormatMap(theme));
        d->m_highlighter->set_rainbow_colors(theme.rainbowColors);
    }
    applyEditorStyle(d->m_editor);
    if (d->m_highlighter) d->m_highlighter->rehighlight();
    d->updateLineNumberAreaWidth(0);
    d->updateCurrentLineHighlight();
    if (d->m_completer)      d->m_completer->setPopupTheme(theme);
    if (d->m_functionPopup)  d->m_functionPopup->setTheme(theme);
}

void CodeEditor::setThemeFromFile(const QString& jsonPath) { setTheme(QEditorTheme::fromJsonFile(jsonPath)); }
QEditorTheme CodeEditor::theme() const { return d_ptr->m_theme; }
void CodeEditor::setEditorFont(const QFont& font) { d_ptr->m_editor->setFont(font); }
QFont CodeEditor::editorFont() const { return d_ptr->m_editor->font(); }

void CodeEditor::setLineNumbersVisible(bool visible) {
    d_ptr->m_gutter->setLineNumbersVisible(visible);
    d_ptr->updateLineNumberAreaWidth(0);
}

void CodeEditor::setMiniMapVisible(bool /*visible*/) { /* stub */ }

void CodeEditor::setFoldingEnabled(bool enabled) {
    d_ptr->m_gutter->setFoldingVisible(enabled);
    d_ptr->updateLineNumberAreaWidth(0);
    d_ptr->setFoldingEnabled(enabled);
}

void CodeEditor::setAutoCompleteEnabled(bool enabled) {
    if (enabled) {
        if (!d_ptr->m_completer) {
            d_ptr->m_completer = new AutoCompleter(d_ptr.get());
            d_ptr->m_completer->setEditor(d_ptr->m_editor);
            d_ptr->m_completer->setPopupTheme(d_ptr->m_theme);
        }
    } else {
        if (d_ptr->m_completer) { d_ptr->m_completer->deleteLater(); d_ptr->m_completer = nullptr; }
    }
}

void CodeEditor::setAutoIndentEnabled (bool e) { d_ptr->m_autoIndent  = e; }
void CodeEditor::setAutoBracketEnabled(bool e) { d_ptr->m_autoBracket = e; }
void CodeEditor::setWordWrap(bool enabled) {
    d_ptr->m_editor->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}
void CodeEditor::setShowWhitespace(bool visible) {
    QTextOption opt = d_ptr->m_editor->document()->defaultTextOption();
    opt.setFlags(visible
                     ? QTextOption::ShowTabsAndSpaces | QTextOption::ShowLineAndParagraphSeparators
                     : QTextOption::Flags());
    d_ptr->m_editor->document()->setDefaultTextOption(opt);
}
void CodeEditor::setTabWidth(int spaces) {
    d_ptr->m_tabWidth = spaces;
    d_ptr->m_editor->setTabStopDistance(
        QFontMetricsF(d_ptr->m_editor->font()).horizontalAdvance(' ') * spaces);
}
void CodeEditor::setInsertSpacesOnTab(bool spaces) { d_ptr->m_insertSpaces = spaces; }

void CodeEditor::addGutterIcon(int line, GutterIconType type, const QString& tooltip) {
    d_ptr->m_icons[qMax(0, line-1)] = {type, tooltip, nullptr};
    d_ptr->m_gutter->setIconMap(d_ptr->m_icons);
}
void CodeEditor::removeGutterIcon(int line) {
    d_ptr->m_icons.remove(qMax(0, line-1));
    d_ptr->m_gutter->setIconMap(d_ptr->m_icons);
}
void CodeEditor::clearGutterIcons() {
    d_ptr->m_icons.clear();
    d_ptr->m_gutter->setIconMap(d_ptr->m_icons);
}

// ── Folding public API ────────────────────────────────────────────────────────

void CodeEditor::foldLine(int line) {
    // isFolded means: is a fold header AND collapsed.
    // We want to fold if the header exists and is not yet collapsed.
    int blockNum = line - 1;
    if (d_ptr->m_foldManager->foldRanges().contains(blockNum)
        && !d_ptr->m_foldManager->isFolded(blockNum))
    {
        d_ptr->m_foldManager->toggleFold(blockNum);
    }
}

void CodeEditor::unfoldLine(int line) {
    int blockNum = line - 1;
    if (d_ptr->m_foldManager->isFolded(blockNum))
        d_ptr->m_foldManager->toggleFold(blockNum);
}

void CodeEditor::foldAll()   { d_ptr->m_foldManager->foldAll();   }
void CodeEditor::unfoldAll() { d_ptr->m_foldManager->unfoldAll(); }

// ── Search & replace ──────────────────────────────────────────────────────────

void CodeEditor::showSearchBar() {}
void CodeEditor::hideSearchBar() {}

static void highlightMatches(QTextDocument* doc, const QString& term,
                             bool caseSensitive, bool regex,
                             QList<QTextEdit::ExtraSelection>& sel,
                             const QEditorTheme& theme)
{
    sel.clear();
    if (term.isEmpty()) return;
    QTextDocument::FindFlags flags;
    if (caseSensitive) flags |= QTextDocument::FindCaseSensitively;
    QTextCursor cur(doc);
    auto mkRE = [&]{ return QRegularExpression(term,
                                                caseSensitive ? QRegularExpression::NoPatternOption
                                                              : QRegularExpression::CaseInsensitiveOption); };
    while (!cur.isNull() && !cur.atEnd()) {
        cur = regex ? doc->find(mkRE(), cur) : doc->find(term, cur, flags);
        if (!cur.isNull()) {
            QTextEdit::ExtraSelection s;
            s.format.setBackground(theme.searchHighlightBackground);
            s.format.setForeground(theme.searchHighlightForeground);
            s.cursor = cur;
            sel.append(s);
        }
    }
}

int CodeEditor::findNext(const QString& term, bool caseSensitive, bool regex) {
    QTextDocument::FindFlags flags;
    if (caseSensitive) flags |= QTextDocument::FindCaseSensitively;
    auto mkRE = [&]{ return QRegularExpression(term,
                                                caseSensitive ? QRegularExpression::NoPatternOption
                                                              : QRegularExpression::CaseInsensitiveOption); };
    QTextCursor cur = d_ptr->m_editor->textCursor();
    QTextCursor match = regex ? d_ptr->m_editor->document()->find(mkRE(), cur)
                              : d_ptr->m_editor->document()->find(term, cur, flags);
    if (match.isNull()) {
        cur.movePosition(QTextCursor::Start);
        match = regex ? d_ptr->m_editor->document()->find(mkRE(), cur)
                      : d_ptr->m_editor->document()->find(term, cur, flags);
    }
    highlightMatches(d_ptr->m_editor->document(), term, caseSensitive, regex,
                     d_ptr->m_searchSelections, d_ptr->m_theme);
    d_ptr->updateCurrentLineHighlight();
    if (!match.isNull()) { d_ptr->m_editor->setTextCursor(match); d_ptr->m_editor->centerCursor(); return match.selectionStart(); }
    return -1;
}

int CodeEditor::findPrev(const QString& term, bool caseSensitive, bool regex) {
    QTextDocument::FindFlags flags = QTextDocument::FindBackward;
    if (caseSensitive) flags |= QTextDocument::FindCaseSensitively;
    auto mkRE = [&]{ return QRegularExpression(term,
                                                caseSensitive ? QRegularExpression::NoPatternOption
                                                              : QRegularExpression::CaseInsensitiveOption); };
    QTextCursor cur = d_ptr->m_editor->textCursor();
    QTextCursor match = regex ? d_ptr->m_editor->document()->find(mkRE(), cur, QTextDocument::FindBackward)
                              : d_ptr->m_editor->document()->find(term, cur, flags);
    if (match.isNull()) {
        cur.movePosition(QTextCursor::End);
        match = regex ? d_ptr->m_editor->document()->find(mkRE(), cur, QTextDocument::FindBackward)
                      : d_ptr->m_editor->document()->find(term, cur, flags);
    }
    highlightMatches(d_ptr->m_editor->document(), term, caseSensitive, regex,
                     d_ptr->m_searchSelections, d_ptr->m_theme);
    d_ptr->updateCurrentLineHighlight();
    if (!match.isNull()) { d_ptr->m_editor->setTextCursor(match); d_ptr->m_editor->centerCursor(); return match.selectionStart(); }
    return -1;
}

void CodeEditor::replaceNext(const QString& term, const QString& replacement) {
    if (d_ptr->m_editor->textCursor().hasSelection()
        && d_ptr->m_editor->textCursor().selectedText() == term)
        d_ptr->m_editor->textCursor().insertText(replacement);
    findNext(term, true, false);
}

void CodeEditor::replaceAll(const QString& term, const QString& replacement) {
    QTextCursor cur(d_ptr->m_editor->document());
    cur.beginEditBlock();
    while (!(cur = d_ptr->m_editor->document()->find(term, cur)).isNull())
        cur.insertText(replacement);
    cur.endEditBlock();
    findNext(term, true, false);
}

void CodeEditor::goToLine(int line) {
    QTextBlock block = d_ptr->m_editor->document()->findBlockByNumber(qMax(0, line-1));
    if (block.isValid()) {
        QTextCursor cursor(block);
        d_ptr->m_editor->setTextCursor(cursor);
        d_ptr->m_editor->centerCursor();
    }
}

int     CodeEditor::currentLine()   const { return d_ptr->m_editor->textCursor().blockNumber() + 1; }
int     CodeEditor::currentColumn() const { return d_ptr->m_editor->textCursor().columnNumber() + 1; }
QString CodeEditor::selectedText()  const { return d_ptr->m_editor->textCursor().selectedText(); }
void    CodeEditor::selectAll()           { d_ptr->m_editor->selectAll(); }

void CodeEditor::setCustomKeywords(const QStringList& kw) { if (d_ptr->m_completer) d_ptr->m_completer->setCustomKeywords(kw); }
void CodeEditor::addCustomKeyword (const QString& kw)     { if (d_ptr->m_completer) d_ptr->m_completer->addCustomKeyword(kw); }
void CodeEditor::setReadOnly(bool r) { d_ptr->m_editor->setReadOnly(r); }
bool CodeEditor::isReadOnly() const  { return d_ptr->m_editor->isReadOnly(); }

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    d_ptr->updateLineNumberAreaWidth(0);
}

// ── Function list popup ───────────────────────────────────────────────────────

void CodeEditor::showFunctionList()
{
    if (d_ptr->m_functionPopup && d_ptr->m_functionPopup->isEmpty())
        d_ptr->updateFunctionList();
    if (d_ptr->m_functionPopup)
        d_ptr->m_functionPopup->showBelowWidget(this);
}

QVector<CodeEditor::FunctionInfo> CodeEditor::getFunctionList() const
{
    QVector<FunctionInfo> result;
    TreeSitterHelper helper(d_ptr->m_editor->toPlainText());
    for (const auto& func : helper.functions) {
        FunctionInfo info;
        info.name       = func.signature.split('(').first().trimmed();
        info.signature  = func.signature;
        info.lineNumber = func.startLine + 1;
        result.append(info);
    }
    return result;
}
