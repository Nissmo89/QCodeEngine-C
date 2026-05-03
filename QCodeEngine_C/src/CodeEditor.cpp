#include "CodeEditor/CodeEditor.h"
#include "CodeEditor/diagnosticmanager.h"
#include "CodeEditor_p.h"
#include <QHBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <QTextBlock>
#include <QPainter>
#include <QVector>
#include <QTimer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QThread>
#include <memory>
#include <algorithm>
#include "TreeSitterQuery_C.h"
#include "syntaxerrordetector.h"

extern "C" const TSLanguage *tree_sitter_c(void);

// ── Bracket matching ─────────────────────────────────────────────────────────

enum LargeFileAnchorMode {
    LargeFileAnchorTop = 0,
    LargeFileAnchorBottom = 1,
    LargeFileAnchorCenter = 2,
};

struct LargeFileState {
    QFile file;
    uchar* mapped = nullptr;
    qint64 fileSize = 0;
    qint64 windowStartByte = 0;
    qint64 windowEndByte = 0;
    qint64 windowBytes = 512 * 1024;
    qint64 overlapBytes = 64 * 1024;
    int currentWindowFirstLine = 1;
    int currentWindowLineCount = 0;
    int requestId = 0;
    bool loading = false;
    bool ignoreScroll = false;
    bool indexingReady = false;
    int pendingAnchorMode = LargeFileAnchorTop;
    qint64 pendingByte = -1;
    QVector<qint64> lineOffsets;
    QThread* chunkThread = nullptr;
    QThread* indexThread = nullptr;
};

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

QString documentSlice(QTextDocument* doc, int start, int end)
{
    if (!doc)
        return {};

    const int maxPos = qMax(0, doc->characterCount() - 1);
    start = qBound(0, start, maxPos);
    end = qBound(0, end, maxPos);
    if (end <= start)
        return {};

    QTextCursor cursor(doc);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return text;
}

static qint64 clampLargeFileByte(qint64 value, qint64 fileSize)
{
    return qBound<qint64>(0, value, qMax<qint64>(0, fileSize));
}

static qint64 findLineStartByte(const uchar* data, qint64 fileSize, qint64 pos)
{
    qint64 p = clampLargeFileByte(pos, fileSize);
    while (p > 0 && data[p - 1] != '\n')
        --p;
    return p;
}

static qint64 findLineEndByteExclusive(const uchar* data, qint64 fileSize, qint64 pos)
{
    qint64 p = clampLargeFileByte(pos, fileSize);
    while (p < fileSize && data[p] != '\n')
        ++p;
    if (p < fileSize)
        ++p;
    return p;
}

static int countLinesInUtf8Chunk(const QString& text)
{
    if (text.isEmpty())
        return 1;
    return text.count(QLatin1Char('\n')) + 1;
}

static constexpr qint64 kAsyncLoadThreshold = 1024 * 1024;
static constexpr qsizetype kAsyncInsertChunkChars = 128 * 1024;
static constexpr qint64 kLargeDocumentModeThreshold = 2LL * 1024 * 1024;
static constexpr qint64 kLargeDocumentModeDisableThreshold = 1536LL * 1024;
static constexpr qint64 kWindowedLargeFileThreshold = 32LL * 1024 * 1024;

static int largeDocumentHighlightRadius(const QPlainTextEdit* editor)
{
    if (!editor)
        return 220;

    const int lineHeight = qMax(1, editor->fontMetrics().height());
    const int visibleLines = qMax(1, editor->viewport()->height() / lineHeight);
    const int radius = visibleLines * 3;
    return qBound(160, radius, 420);
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
    m_largeFileState = new LargeFileState;
    setupLayout();
    setupHighlighter();
    setupEditorModules();
    setupConnections();
    setupActions();

    applyEditorStyle(m_editor);
    updateLineNumberAreaWidth(0);

    QTimer::singleShot(0, this, [this]() {
        m_gutter->updateWidth();
        m_gutter->update();
    });
}

// ── Layout ────────────────────────────────────────────────────────────────────
void CodeEditorPrivate::setupLayout()
{
    m_gutter = new GutterWidget(m_editor, q_ptr);

    QHBoxLayout* layout = new QHBoxLayout(q_ptr);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_gutter);
    layout->addWidget(m_editor);
}

// ── Tree-sitter highlighter ───────────────────────────────────────────────────
void CodeEditorPrivate::setupHighlighter()
{
    m_highlighter = new TreeSitterHighlighter(
        tree_sitter_c(),
        std::string(HIGHLIGHTS_SCM),
        generateFormatMap(m_theme),
        m_editor->document());

    m_highlighter->set_rainbow_colors(m_theme.rainbowColors);
}

// ── Editor modules (all borrow TSTree* from highlighter) ─────────────────────
void CodeEditorPrivate::setupEditorModules()
{
    // FoldManager
    m_foldManager = new FoldManager(this);
    m_foldManager->setDocument(m_editor->document());

    // LineHighlighter — notebook-style {N,#COLOR} comment tags
    m_lineHighlighter = new LineHighlighter(this);
    m_lineHighlighter->setDocument(m_editor->document());
    m_lineHighlighter->setEditor(m_editor);

    // DiagnosticManager — owns squiggle rendering
    m_diagnosticManager = new DiagnosticManager(this);
    m_diagnosticManager->setDocument(m_editor->document());
    m_diagnosticManager->setErrorColor  (m_theme.diagnosticError);
    m_diagnosticManager->setWarningColor(m_theme.diagnosticWarning);
    m_diagnosticManager->setInfoColor   (m_theme.diagnosticInfo);
    m_diagnosticManager->setHintColor   (m_theme.diagnosticHint);

    // SyntaxErrorDetector — walks TSTree* for ERROR/MISSING nodes,
    // feeds DiagnosticManager, and gates TCC compilation
    m_syntaxChecker = new SyntaxErrorDetector(this);
    m_syntaxChecker->setDocument(m_editor->document());
    m_syntaxChecker->setDiagnosticManager(m_diagnosticManager);

    // AutoCompleter
    m_completer = new AutoCompleter(this);
    m_completer->setEditor(m_editor);

    m_largeDocHighlightTimer = new QTimer(this);
    m_largeDocHighlightTimer->setSingleShot(true);
    m_largeDocHighlightTimer->setInterval(80);
    connect(m_largeDocHighlightTimer, &QTimer::timeout, this, [this]() {
        if (!m_largeDocumentMode || !m_highlighter || m_heavyFeaturesSuspended)
            return;
        const int line = (m_pendingLargeDocHighlightLine >= 0)
                             ? m_pendingLargeDocHighlightLine
                             : m_editor->textCursor().blockNumber();
        m_highlighter->rehighlightAroundBlock(
            line, largeDocumentHighlightRadius(m_editor));
    });

    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
}

// ── Signal / slot wiring ──────────────────────────────────────────────────────
void CodeEditorPrivate::setupConnections()
{
    // ── "parsed" fan-out: one slot per module, all zero extra parse cost ──────
    connect(m_highlighter, &TreeSitterHighlighter::parsed,
            this, &CodeEditorPrivate::onTreeParsed);

    // ── Gutter refresh after fold state changes ───────────────────────────────
    connect(m_foldManager, &FoldManager::foldRangesUpdated,
            this, &CodeEditorPrivate::updateGutterFoldRanges);

    connect(m_foldManager, &FoldManager::foldStateChanged, this, [this]() {
        updateGutterFoldRanges();
        m_editor->viewport()->update();
    });

    // ── Line highlight → merge into extra-selection list ─────────────────────
    connect(m_lineHighlighter, &LineHighlighter::highlightChanged,
            this, [this]() {
                m_lineHighlightSelections = m_lineHighlighter->extraSelections();
                updateCurrentLineHighlight();
            });

    // // ── TCC gate: disable compile action while syntax errors are present ──────
    // connect(m_syntaxChecker, &SyntaxErrorDetector::syntaxStateChanged,
    //         this, [this](bool clean) {
    //             m_tccCompileAction->setEnabled(clean);
    //         });

    // ── Editor signals ────────────────────────────────────────────────────────
    connect(m_editor->document(), &QTextDocument::contentsChange,
            m_editor, [this](int from, int /*removed*/, int added) {
                if (m_asyncLoadInProgress || m_largeDocumentMode)
                    return;
                enforceFixedLineHeight(from, added);
            });

    connect(m_editor, &QPlainTextEdit::blockCountChanged,
            this, &CodeEditorPrivate::updateLineNumberAreaWidth);

    connect(m_editor, &QPlainTextEdit::cursorPositionChanged,
            this, &CodeEditorPrivate::onCursorPositionChanged);

    connect(m_editor, &QPlainTextEdit::textChanged,
            this, &CodeEditorPrivate::onTextChanged);

    connect(m_editor->document(), &QTextDocument::modificationChanged,
            this, [this](bool modified) {
                emit q_ptr->documentModifiedChanged(modified);
            });

    connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &CodeEditorPrivate::onLargeFileScroll);

    // ── Gutter interactions ───────────────────────────────────────────────────
    connect(m_gutter, &GutterWidget::foldToggled,
            this, &CodeEditorPrivate::onGutterFoldClicked);

    connect(m_gutter, &GutterWidget::markerToggled,
            this, &CodeEditorPrivate::onGutterMarkerToggled);

}

// ── Actions & shortcuts ───────────────────────────────────────────────────────
void CodeEditorPrivate::setupActions()
{
    // Function list popup
    m_functionPopup = new FloatingListPopup(q_ptr);
    connect(m_functionPopup, &FloatingListPopup::functionSelected,
            this, &CodeEditorPrivate::onFunctionSelected);

    m_searchBar = new FindReplaceBar(q_ptr);
    m_searchBar->setEditor(m_editor);
    m_searchBar->setTheme(m_theme);
    m_searchBar->setHighlightsHandler([this](const QList<QTextEdit::ExtraSelection>& selections) {
        m_searchSelections = selections;
        updateCurrentLineHighlight();
    });

    // Debounced: full parse is expensive, don't fire on every keystroke
    m_functionListTimer = new QTimer(this);
    m_functionListTimer->setSingleShot(true);
    m_functionListTimer->setInterval(500);
    connect(m_editor->document(), &QTextDocument::contentsChanged,
            this, [this]() {
                if (m_largeFileMode || m_largeDocumentMode)
                    return;
                m_functionListTimer->start();
            });
    connect(m_functionListTimer, &QTimer::timeout,
            this, &CodeEditorPrivate::updateFunctionList);

    QAction* showFunctionsAction = new QAction(q_ptr);
    showFunctionsAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(showFunctionsAction, &QAction::triggered, q_ptr, &CodeEditor::showFunctionList);
    q_ptr->addAction(showFunctionsAction);

    QAction* showSearchAction = new QAction(q_ptr);
    showSearchAction->setShortcut(QKeySequence::Find);
    connect(showSearchAction, &QAction::triggered, q_ptr, &CodeEditor::showSearchBar);
    q_ptr->addAction(showSearchAction);

    QAction* showReplaceAction = new QAction(q_ptr);
    showReplaceAction->setShortcut(QKeySequence::Replace);
    connect(showReplaceAction, &QAction::triggered, this, [this]() {
        if (m_searchBar)
            m_searchBar->openFindReplace();
    });
    q_ptr->addAction(showReplaceAction);
}

// ── "parsed" fan-out slot ─────────────────────────────────────────────────────
// Single entry point for all TSTree* consumers. Order matters:
//   1. Folding first  — updates block visibility
//   2. Line highlights — reads block structure
//   3. Syntax errors  — walks tree, feeds DiagnosticManager
void CodeEditorPrivate::onTreeParsed(void* treePtr)
{
    if (m_largeDocumentMode)
        return;

    if (m_foldingEnabled)
        m_foldManager->updateFoldRanges(treePtr, m_editor->document());

    m_lineHighlighter->updateFromTree(treePtr, m_editor->document());

    m_syntaxChecker->analyze(treePtr);
}

// ── Helpers ───────────────────────────────────────────────────────────────────
void CodeEditorPrivate::enforceFixedLineHeight(int from, int charsAdded)
{
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
}

void CodeEditorPrivate::onGutterMarkerToggled(int line, MarkerType type)
{
    GutterIconType iconType = GutterIconType::Info;
    if      (type == MarkerType::Error)      iconType = GutterIconType::Error;
    else if (type == MarkerType::Warning)    iconType = GutterIconType::Warning;
    else if (type == MarkerType::Breakpoint) iconType = GutterIconType::Breakpoint;
    else if (type == MarkerType::Bookmark)   iconType = GutterIconType::Bookmark;
    else if (type == MarkerType::Tracepoint) iconType = GutterIconType::Tracepoint;
    emit q_ptr->gutterIconClicked(line, iconType);
}

bool CodeEditorPrivate::shouldUseLargeFileMode(qint64 fileSize) const
{
    return fileSize >= kWindowedLargeFileThreshold;
}

bool CodeEditorPrivate::shouldUseLargeDocumentMode(qint64 sourceBytesHint) const
{
    qint64 bytes = sourceBytesHint;
    if (bytes < 0) {
        bytes = static_cast<qint64>(m_editor->document()->characterCount()) * 2;
    }
    if (m_largeDocumentMode)
        return bytes >= kLargeDocumentModeDisableThreshold;
    return bytes >= kLargeDocumentModeThreshold;
}

void CodeEditorPrivate::applyDocumentPerformanceMode(qint64 sourceBytesHint)
{
    const bool enabled = shouldUseLargeDocumentMode(sourceBytesHint);
    if (m_largeDocumentMode == enabled) {
        if (m_highlighter)
            m_highlighter->setPerformanceMode(enabled);
        if (m_completer)
            m_completer->setLargeDocumentMode(enabled);
        if (m_searchBar)
            m_searchBar->setHighlightAllLimit(enabled ? 1500 : 0);
        return;
    }

    m_largeDocumentMode = enabled;

    if (m_highlighter)
        m_highlighter->setPerformanceMode(enabled);
    if (m_completer)
        m_completer->setLargeDocumentMode(enabled);
    if (m_searchBar)
        m_searchBar->setHighlightAllLimit(enabled ? 1500 : 0);

    if (enabled) {
        if (m_foldManager)
            m_foldManager->unfoldAll();
        if (m_functionListTimer)
            m_functionListTimer->stop();
        if (m_functionPopup)
            m_functionPopup->clear();
        if (m_functionPopup)
            m_functionPopup->hide();
        if (m_lineHighlighter)
            m_lineHighlighter->clear();
        if (m_diagnosticManager)
            m_diagnosticManager->clear();
        m_lineHighlightSelections.clear();
        if (m_gutter) {
            m_gutter->setFoldRanges({});
            m_gutter->setFoldingVisible(false);
        }
        m_pendingLargeDocHighlightLine = m_editor->textCursor().blockNumber();
        if (m_largeDocHighlightTimer)
            m_largeDocHighlightTimer->start();
    } else {
        if (m_largeDocHighlightTimer)
            m_largeDocHighlightTimer->stop();
        if (m_gutter)
            m_gutter->setFoldingVisible(m_foldingEnabled);
        if (m_highlighter && !m_heavyFeaturesSuspended)
            m_highlighter->rehighlight();
    }

    updateCurrentLineHighlight();
}

bool CodeEditorPrivate::shouldUseAsyncFullLoad(qint64 fileSize) const
{
    return fileSize >= kAsyncLoadThreshold;
}

void CodeEditorPrivate::suspendHeavyEditorFeatures()
{
    m_heavyFeaturesSuspended = true;
    if (m_highlighter)
        m_highlighter->set_document(nullptr);
    if (m_lineHighlighter)
        m_lineHighlighter->clear();
    if (m_foldManager)
        m_foldManager->unfoldAll();
    if (m_gutter)
        m_gutter->setFoldRanges({});

    m_bracketSelections.clear();
    m_searchSelections.clear();
    m_lineHighlightSelections.clear();
    updateCurrentLineHighlight();
}

void CodeEditorPrivate::resumeHeavyEditorFeatures()
{
    m_heavyFeaturesSuspended = false;
    if (m_highlighter)
        m_highlighter->set_document(m_editor->document());
    if (m_highlighter)
        m_highlighter->setPerformanceMode(m_largeDocumentMode);
    if (m_lineHighlighter) {
        m_lineHighlighter->setDocument(m_editor->document());
        m_lineHighlighter->setEditor(m_editor);
    }
    if (m_foldManager)
        m_foldManager->setDocument(m_editor->document());
    if (m_diagnosticManager)
        m_diagnosticManager->setDocument(m_editor->document());
    if (m_syntaxChecker)
        m_syntaxChecker->setDocument(m_editor->document());
    if (m_completer)
        m_completer->setLargeDocumentMode(m_largeDocumentMode);

    if (m_foldingEnabled && m_gutter)
        m_gutter->setFoldingVisible(!m_largeDocumentMode);
}

void CodeEditorPrivate::cancelAsyncFileLoad()
{
    ++m_asyncLoadGeneration;
    m_asyncLoadInProgress = false;
    m_asyncLoadedText.clear();
    m_asyncLoadedPath.clear();
    m_asyncLoadedBytes = 0;
    m_asyncLoadOffset = 0;

    if (m_asyncLoadThread) {
        disconnect(m_asyncLoadThread, nullptr, this, nullptr);
        m_asyncLoadThread->quit();
        m_asyncLoadThread->wait();
        delete m_asyncLoadThread;
        m_asyncLoadThread = nullptr;
    }
}

bool CodeEditorPrivate::startAsyncFileLoad(const QString& filePath)
{
    cancelAsyncFileLoad();
    exitLargeFileMode();

    auto file = std::make_shared<QFile>(filePath);
    if (!file->open(QIODevice::ReadOnly))
        return false;

    const qint64 fileSize = file->size();
    uchar* mapped = file->map(0, fileSize);
    if (!mapped) {
        file->close();
        return false;
    }

    auto decodedText = std::make_shared<QString>();
    const int generation = ++m_asyncLoadGeneration;
    m_asyncLoadInProgress = true;
    m_asyncLoadedPath = filePath;
    m_asyncLoadedBytes = fileSize;
    m_savedReadOnly = m_editor->isReadOnly();
    suspendHeavyEditorFeatures();
    m_editor->setUndoRedoEnabled(false);
    m_editor->setReadOnly(true);
    m_editor->setPlainText(QStringLiteral("Loading file asynchronously..."));

    m_asyncLoadThread = QThread::create([mapped, fileSize, decodedText]() {
        *decodedText = QString::fromUtf8(
            reinterpret_cast<const char*>(mapped),
            static_cast<qsizetype>(fileSize));
    });

    connect(m_asyncLoadThread, &QThread::finished, this,
            [this, decodedText, generation, filePath, file, mapped]() mutable {
                file->unmap(mapped);
                file->close();

                if (generation == m_asyncLoadGeneration)
                    beginChunkedTextApply(std::move(*decodedText), filePath);

                if (m_asyncLoadThread) {
                    delete m_asyncLoadThread;
                    m_asyncLoadThread = nullptr;
                }
            });
    m_asyncLoadThread->start();
    return true;
}

void CodeEditorPrivate::beginChunkedTextApply(QString text, const QString& filePath)
{
    m_asyncLoadedText = std::move(text);
    m_asyncLoadedPath = filePath;
    m_asyncLoadOffset = 0;
    m_editor->clear();
    if (!shouldUseLargeDocumentMode(m_asyncLoadedBytes))
        applyEditorStyle(m_editor);
    applyNextTextChunk(m_asyncLoadGeneration);
}

void CodeEditorPrivate::applyNextTextChunk(int generation)
{
    if (generation != m_asyncLoadGeneration)
        return;

    if (m_asyncLoadOffset >= m_asyncLoadedText.size()) {
        m_asyncLoadInProgress = false;
        m_editor->setReadOnly(m_savedReadOnly);
        m_editor->setUndoRedoEnabled(true);
        resumeHeavyEditorFeatures();
        applyDocumentPerformanceMode(m_asyncLoadedBytes);
        if (!m_largeDocumentMode)
            applyEditorStyle(m_editor);
        if (m_highlighter) {
            if (m_largeDocumentMode) {
                QTimer::singleShot(0, this, [this]() {
                    if (!m_heavyFeaturesSuspended && m_highlighter)
                        m_highlighter->rehighlightAroundBlock(
                            m_editor->textCursor().blockNumber(),
                            largeDocumentHighlightRadius(m_editor));
                });
            } else {
                m_highlighter->rehighlight();
            }
        }
        m_gutter->updateWidth();
        m_gutter->update();
        emit q_ptr->fileLoaded(m_asyncLoadedPath);
        m_asyncLoadedText.clear();
        m_asyncLoadedPath.clear();
        m_asyncLoadedBytes = 0;
        return;
    }

    const qsizetype remaining = m_asyncLoadedText.size() - m_asyncLoadOffset;
    const qsizetype chunkLen = qMin<qsizetype>(kAsyncInsertChunkChars, remaining);
    QTextCursor cursor(m_editor->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(m_asyncLoadedText.mid(m_asyncLoadOffset, chunkLen));
    m_asyncLoadOffset += chunkLen;

    QTimer::singleShot(0, this, [this, generation]() {
        applyNextTextChunk(generation);
    });
}

void CodeEditorPrivate::exitLargeFileMode()
{
    if (!m_largeFileState || !m_largeFileMode)
        return;

    if (m_largeFileState->chunkThread) {
        disconnect(m_largeFileState->chunkThread, nullptr, this, nullptr);
        m_largeFileState->chunkThread->quit();
        m_largeFileState->chunkThread->wait();
        delete m_largeFileState->chunkThread;
        m_largeFileState->chunkThread = nullptr;
    }
    if (m_largeFileState->indexThread) {
        disconnect(m_largeFileState->indexThread, nullptr, this, nullptr);
        m_largeFileState->indexThread->quit();
        m_largeFileState->indexThread->wait();
        delete m_largeFileState->indexThread;
        m_largeFileState->indexThread = nullptr;
    }

    if (m_largeFileState->mapped) {
        m_largeFileState->file.unmap(m_largeFileState->mapped);
        m_largeFileState->mapped = nullptr;
    }
    if (m_largeFileState->file.isOpen())
        m_largeFileState->file.close();

    m_largeFileState->fileSize = 0;
    m_largeFileState->windowStartByte = 0;
    m_largeFileState->windowEndByte = 0;
    m_largeFileState->currentWindowFirstLine = 1;
    m_largeFileState->currentWindowLineCount = 0;
    m_largeFileState->requestId = 0;
    m_largeFileState->loading = false;
    m_largeFileState->ignoreScroll = false;
    m_largeFileState->indexingReady = false;
    m_largeFileState->pendingByte = -1;
    m_largeFileState->lineOffsets.clear();

    m_largeFileMode = false;
    m_editor->setReadOnly(m_savedReadOnly);
    m_editor->setUndoRedoEnabled(true);
    resumeHeavyEditorFeatures();
}

bool CodeEditorPrivate::enterLargeFileMode(const QString& filePath)
{
    exitLargeFileMode();

    m_largeFileState->file.setFileName(filePath);
    if (!m_largeFileState->file.open(QIODevice::ReadOnly)) {
        return false;
    }

    m_largeFileState->mapped = m_largeFileState->file.map(0, m_largeFileState->file.size());
    if (!m_largeFileState->mapped) {
        m_largeFileState->file.close();
        return false;
    }

    m_largeFileMode = true;
    m_largeFileState->fileSize = m_largeFileState->file.size();
    m_largeFileState->requestId = 0;
    m_largeFileState->lineOffsets = {0};
    m_savedReadOnly = m_editor->isReadOnly();
    m_editor->setReadOnly(true);
    m_editor->setUndoRedoEnabled(false);

    suspendHeavyEditorFeatures();
    if (m_gutter)
        m_gutter->setFoldingVisible(false);

    m_editor->setPlainText(QStringLiteral("Loading large file..."));
    requestLargeFileWindow(0, LargeFileAnchorTop);
    startLargeFileIndexing();
    return true;
}

void CodeEditorPrivate::requestLargeFileWindow(qint64 requestedByte, int anchorMode)
{
    if (!m_largeFileMode || !m_largeFileState || !m_largeFileState->mapped)
        return;

    LargeFileState* state = m_largeFileState;
    requestedByte = clampLargeFileByte(requestedByte, state->fileSize);
    const qint64 startByte = findLineStartByte(state->mapped, state->fileSize, requestedByte);
    qint64 endByte = startByte + state->windowBytes;
    endByte = findLineEndByteExclusive(state->mapped, state->fileSize, endByte);

    if (state->loading) {
        state->pendingByte = startByte;
        state->pendingAnchorMode = anchorMode;
        return;
    }

    state->loading = true;
    const int requestId = ++state->requestId;
    auto decodedText = std::make_shared<QString>();
    QThread* thread = QThread::create([state, startByte, endByte, decodedText]() {
        const qint64 length = qMax<qint64>(0, endByte - startByte);
        *decodedText = QString::fromUtf8(
            reinterpret_cast<const char*>(state->mapped + startByte),
            static_cast<qsizetype>(length));
    });

    state->chunkThread = thread;
    connect(thread, &QThread::finished, this,
            [this, thread, decodedText, requestId, startByte, endByte, anchorMode]() {
                if (m_largeFileMode)
                    applyLargeFileWindow(requestId, startByte, endByte, *decodedText, anchorMode);

                if (m_largeFileState && m_largeFileState->chunkThread == thread)
                    m_largeFileState->chunkThread = nullptr;

                thread->deleteLater();
            });
    thread->start();
}

void CodeEditorPrivate::applyLargeFileWindow(int requestId, qint64 startByte, qint64 endByte,
                                             const QString& text, int anchorMode)
{
    if (!m_largeFileMode || !m_largeFileState || requestId != m_largeFileState->requestId)
        return;

    LargeFileState* state = m_largeFileState;
    state->loading = false;
    state->windowStartByte = startByte;
    state->windowEndByte = endByte;
    state->currentWindowLineCount = countLinesInUtf8Chunk(text);

    if (state->indexingReady) {
        const auto it = std::upper_bound(state->lineOffsets.begin(), state->lineOffsets.end(), startByte);
        state->currentWindowFirstLine = qMax(1, static_cast<int>(std::distance(state->lineOffsets.begin(), it)));
    } else {
        state->currentWindowFirstLine = 1;
    }

    state->ignoreScroll = true;
    m_editor->setPlainText(text);
    applyEditorStyle(m_editor);
    m_gutter->updateWidth();
    m_gutter->update();
    updateCurrentLineHighlight();

    QScrollBar* bar = m_editor->verticalScrollBar();
    {
        QSignalBlocker blocker(bar);
        if (anchorMode == LargeFileAnchorBottom)
            bar->setValue(qMax(0, static_cast<int>(bar->maximum() * 0.80)));
        else if (anchorMode == LargeFileAnchorCenter)
            bar->setValue(bar->maximum() / 2);
        else
            bar->setValue(0);
    }

    QTimer::singleShot(0, this, [this]() {
        if (m_largeFileState)
            m_largeFileState->ignoreScroll = false;
    });

    if (state->pendingByte >= 0) {
        const qint64 pendingByte = state->pendingByte;
        const int pendingAnchorMode = state->pendingAnchorMode;
        state->pendingByte = -1;
        requestLargeFileWindow(pendingByte, pendingAnchorMode);
    }
}

void CodeEditorPrivate::onLargeFileScroll(int value)
{
    if (!m_largeFileMode || !m_largeFileState || m_largeFileState->ignoreScroll || m_largeFileState->loading)
        return;

    QScrollBar* bar = m_editor->verticalScrollBar();
    if (!bar || bar->maximum() <= 0)
        return;

    if (value >= static_cast<int>(bar->maximum() * 0.85)
        && m_largeFileState->windowEndByte < m_largeFileState->fileSize) {
        requestLargeFileWindow(
            qMax<qint64>(0, m_largeFileState->windowEndByte - m_largeFileState->overlapBytes),
            LargeFileAnchorCenter);
    } else if (value <= static_cast<int>(bar->maximum() * 0.15)
               && m_largeFileState->windowStartByte > 0) {
        requestLargeFileWindow(
            qMax<qint64>(0, m_largeFileState->windowStartByte
                            - (m_largeFileState->windowBytes - m_largeFileState->overlapBytes)),
            LargeFileAnchorBottom);
    }
}

void CodeEditorPrivate::startLargeFileIndexing()
{
    if (!m_largeFileMode || !m_largeFileState || !m_largeFileState->mapped || m_largeFileState->indexThread)
        return;

    LargeFileState* state = m_largeFileState;
    auto lineOffsets = std::make_shared<QVector<qint64>>();
    QThread* thread = QThread::create([state, lineOffsets]() {
        lineOffsets->reserve(static_cast<int>(qMin<qint64>(state->fileSize / 24, 2'000'000)));
        lineOffsets->append(0);
        for (qint64 i = 0; i < state->fileSize; ++i) {
            if (state->mapped[i] == '\n' && i + 1 < state->fileSize)
                lineOffsets->append(i + 1);
        }
    });

    state->indexThread = thread;
    connect(thread, &QThread::finished, this, [this, thread, lineOffsets]() {
        if (m_largeFileMode && m_largeFileState) {
            m_largeFileState->lineOffsets = *lineOffsets;
            m_largeFileState->indexingReady = true;
            const auto it = std::upper_bound(
                m_largeFileState->lineOffsets.begin(),
                m_largeFileState->lineOffsets.end(),
                m_largeFileState->windowStartByte);
            m_largeFileState->currentWindowFirstLine =
                qMax(1, static_cast<int>(std::distance(m_largeFileState->lineOffsets.begin(), it)));
        }
        if (m_largeFileState && m_largeFileState->indexThread == thread)
            m_largeFileState->indexThread = nullptr;
        thread->deleteLater();
    });
    thread->start();
}

qint64 CodeEditorPrivate::largeFileByteForLine(int line) const
{
    if (!m_largeFileMode || !m_largeFileState || !m_largeFileState->indexingReady)
        return -1;

    const int zeroBasedLine = qMax(0, line - 1);
    if (zeroBasedLine >= m_largeFileState->lineOffsets.size())
        return -1;
    return m_largeFileState->lineOffsets[zeroBasedLine];
}

int CodeEditorPrivate::largeFileCurrentLine() const
{
    if (!m_largeFileMode || !m_largeFileState)
        return m_editor->textCursor().blockNumber() + 1;

    return m_largeFileState->currentWindowFirstLine
           + m_editor->textCursor().blockNumber();
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
    if (enabled && m_largeDocumentMode) {
        if (m_gutter) {
            m_gutter->setFoldRanges({});
            m_gutter->setFoldingVisible(false);
        }
        m_foldManager->unfoldAll();
        return;
    }

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
        if (m_gutter)
            m_gutter->setFoldRanges({});
        m_editor->viewport()->update();
        m_foldManager->unfoldAll();
        return;
    }

    // Force a fresh fold computation so arrows/ranges are immediately correct
    // when folding gets enabled on an already-loaded document.
    if (!m_heavyFeaturesSuspended && m_highlighter)
        m_highlighter->rehighlight();
    updateGutterFoldRanges();
    m_editor->viewport()->update();
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
    const int cursorPos = m_editor->textCursor().position();
    const int kBracketSearchWindowChars = m_largeDocumentMode ? 4096 : 32768;
    const int maxCursorPos = qMax(0, doc->characterCount() - 1);
    const int safeCursorPos = qBound(0, cursorPos, maxCursorPos);
    const int startPos = qMax(0, safeCursorPos - kBracketSearchWindowChars);
    const int endPos = qMin(maxCursorPos, safeCursorPos + kBracketSearchWindowChars);
    const QString text = documentSlice(doc, startPos, endPos);
    const int localCursorPos = safeCursorPos - startPos;
    const int idx = bracketIndexAtCursor(text, localCursorPos);
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
        m_bracketSelections.append(makeSel(startPos + idx, 1, partner < 0));
        if (partner >= 0) m_bracketSelections.append(makeSel(startPos + partner, 1, false));
    } else if (isCloseBracket(ch)) {
        int partner = findOpeningPartner(text, mask, idx);
        m_bracketSelections.append(makeSel(startPos + idx, 1, partner < 0));
        if (partner >= 0) m_bracketSelections.append(makeSel(startPos + partner, 1, false));
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
    if (m_largeDocumentMode) {
        m_pendingLargeDocHighlightLine = m_editor->textCursor().blockNumber();
        if (m_largeDocHighlightTimer)
            m_largeDocHighlightTimer->start();
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

void CodeEditorPrivate::onTextChanged()
{
    if (!m_largeFileMode && !m_asyncLoadInProgress) {
        const qint64 approxBytes = static_cast<qint64>(m_editor->document()->characterCount()) * 2;
        if (m_largeDocumentMode != shouldUseLargeDocumentMode(approxBytes))
            applyDocumentPerformanceMode(approxBytes);
    }
    emit q_ptr->textChanged();
}

void CodeEditorPrivate::onGutterFoldClicked(int line, bool /*folded*/)
{
    // GutterWidget uses 1-based lines; FoldManager uses 0-based
    m_foldManager->toggleFold(qMax(0, line - 1));
    // foldStateChanged signal → updateGutterFoldRanges + viewport update
}

void CodeEditorPrivate::updateFunctionList()
{
    if (m_largeFileMode || m_largeDocumentMode)
        return;

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

CodeEditor::~CodeEditor()
{
    if (d_ptr) {
        d_ptr->cancelAsyncFileLoad();
        d_ptr->exitLargeFileMode();
        delete d_ptr->m_largeFileState;
        d_ptr->m_largeFileState = nullptr;
    }
}

void CodeEditor::setText(const QString& text) {
    d_ptr->cancelAsyncFileLoad();
    d_ptr->exitLargeFileMode();
    const qint64 approxBytes = static_cast<qint64>(text.size()) * 2;
    const bool suspendForBulkSet = approxBytes >= kAsyncLoadThreshold;
    const bool suspendedHere = suspendForBulkSet && !d_ptr->m_heavyFeaturesSuspended;
    if (suspendedHere)
        d_ptr->suspendHeavyEditorFeatures();
    else if (d_ptr->m_heavyFeaturesSuspended)
        d_ptr->resumeHeavyEditorFeatures();

    d_ptr->m_editor->setPlainText(text);
    if (suspendedHere)
        d_ptr->resumeHeavyEditorFeatures();

    d_ptr->applyDocumentPerformanceMode(approxBytes);
    if (!d_ptr->m_largeDocumentMode)
        applyEditorStyle(d_ptr->m_editor);
    if (d_ptr->m_highlighter) {
        if (d_ptr->m_largeDocumentMode) {
            QTimer::singleShot(0, this, [this]() {
                if (!d_ptr->m_heavyFeaturesSuspended && d_ptr->m_highlighter)
                    d_ptr->m_highlighter->rehighlightAroundBlock(
                        d_ptr->m_editor->textCursor().blockNumber(),
                        largeDocumentHighlightRadius(d_ptr->m_editor));
            });
        } else {
            d_ptr->m_highlighter->rehighlight();
        }
    }
    d_ptr->m_gutter->updateWidth();
    d_ptr->m_gutter->update();
    QTimer::singleShot(0, this, [this]() {
        d_ptr->m_gutter->updateWidth();
        d_ptr->m_gutter->update();
    });
}

QString CodeEditor::text() const { return d_ptr->m_editor->toPlainText(); }

void CodeEditor::insertText(const QString& text) {
    if (d_ptr->m_largeFileMode)
        return;

    QTextCursor tc = d_ptr->m_editor->textCursor();
    tc.insertText(text);
    d_ptr->m_editor->setTextCursor(tc);
}

void CodeEditor::clear() {
    d_ptr->cancelAsyncFileLoad();
    d_ptr->exitLargeFileMode();
    if (d_ptr->m_heavyFeaturesSuspended)
        d_ptr->resumeHeavyEditorFeatures();
    d_ptr->m_editor->clear();
    d_ptr->applyDocumentPerformanceMode(0);
}

bool CodeEditor::loadFile(const QString& filePath) {
    d_ptr->cancelAsyncFileLoad();
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const qint64 fileSize = f.size();
    f.close();

    if (d_ptr->shouldUseLargeFileMode(fileSize)) {
        if (!d_ptr->enterLargeFileMode(filePath))
            return false;
        emit fileLoaded(filePath);
    } else if (d_ptr->shouldUseAsyncFullLoad(fileSize)) {
        return d_ptr->startAsyncFileLoad(filePath);
    } else {
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        if (d_ptr->m_heavyFeaturesSuspended)
            d_ptr->resumeHeavyEditorFeatures();
        setText(QString::fromUtf8(f.readAll()));
        emit fileLoaded(filePath);
    }
    return true;
}

bool CodeEditor::saveFile(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    if (d_ptr->m_largeFileMode && d_ptr->m_largeFileState && d_ptr->m_largeFileState->mapped) {
        f.write(reinterpret_cast<const char*>(d_ptr->m_largeFileState->mapped),
                d_ptr->m_largeFileState->fileSize);
    } else {
        f.write(text().toUtf8());
    }
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
    if (!d->m_largeDocumentMode)
        applyEditorStyle(d->m_editor);
    if (d->m_highlighter) {
        if (d->m_largeDocumentMode)
            d->m_highlighter->rehighlightAroundBlock(
                d->m_editor->textCursor().blockNumber(),
                largeDocumentHighlightRadius(d->m_editor));
        else
            d->m_highlighter->rehighlight();
    }
    d->updateLineNumberAreaWidth(0);
    d->updateCurrentLineHighlight();
    if (d->m_completer)      d->m_completer->setPopupTheme(theme);
    if (d->m_functionPopup)  d->m_functionPopup->setTheme(theme);
    if (d->m_searchBar)      d->m_searchBar->setTheme(theme);
}

void CodeEditor::setThemeFromFile(const QString& jsonPath) { setTheme(QEditorTheme::fromJsonFile(jsonPath)); }
QEditorTheme CodeEditor::theme() const { return d_ptr->m_theme; }
void CodeEditor::setEditorFont(const QFont& font) { d_ptr->m_editor->setFont(font); }
QFont CodeEditor::editorFont() const { return d_ptr->m_editor->font(); }

void CodeEditor::setLineNumbersVisible(bool visible) {
    d_ptr->m_gutter->setLineNumbersVisible(visible);
    d_ptr->updateLineNumberAreaWidth(0);
}

void CodeEditor::setMiniMapVisible(bool visible)
{
    d_ptr->m_miniMapVisibleRequested = visible;
    static bool warned = false;
    if (!warned) {
        qWarning("CodeEditor::setMiniMapVisible(): minimap is not implemented yet.");
        warned = true;
    }
}

void CodeEditor::setFoldingEnabled(bool enabled) {
    d_ptr->m_gutter->setFoldingVisible(enabled && !d_ptr->m_largeDocumentMode);
    d_ptr->updateLineNumberAreaWidth(0);
    d_ptr->setFoldingEnabled(enabled);
}

void CodeEditor::setAutoCompleteEnabled(bool enabled) {
    if (enabled) {
        if (!d_ptr->m_completer) {
            d_ptr->m_completer = new AutoCompleter(d_ptr.get());
            d_ptr->m_completer->setEditor(d_ptr->m_editor);
            d_ptr->m_completer->setPopupTheme(d_ptr->m_theme);
            d_ptr->m_completer->setLargeDocumentMode(d_ptr->m_largeDocumentMode);
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
    d_ptr->m_icons[qMax(1, line)] = {type, tooltip, nullptr};
    d_ptr->m_gutter->setIconMap(d_ptr->m_icons);
}
void CodeEditor::removeGutterIcon(int line) {
    d_ptr->m_icons.remove(qMax(1, line));
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

void CodeEditor::showSearchBar()
{
    if (d_ptr->m_searchBar)
        d_ptr->m_searchBar->openFind();
}

void CodeEditor::hideSearchBar()
{
    if (d_ptr->m_searchBar)
        d_ptr->m_searchBar->closeFindBar();
}

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

static QRegularExpression buildSearchRegex(const QString& term, bool caseSensitive)
{
    return QRegularExpression(
        term,
        caseSensitive ? QRegularExpression::NoPatternOption
                      : QRegularExpression::CaseInsensitiveOption);
}

static bool selectionMatchesTerm(const QString& selectedText,
                                 const QString& term,
                                 bool caseSensitive,
                                 bool regex)
{
    if (regex) {
        const QRegularExpression re = buildSearchRegex(term, caseSensitive);
        if (!re.isValid())
            return false;

        const QRegularExpressionMatch match = re.match(selectedText);
        return match.hasMatch() && match.capturedStart() == 0
               && match.capturedLength() == selectedText.size();
    }

    return selectedText.compare(
               term,
               caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0;
}

static QString buildReplacementText(const QString& selectedText,
                                    const QString& term,
                                    const QString& replacement,
                                    bool caseSensitive,
                                    bool regex)
{
    if (!regex)
        return replacement;

    const QRegularExpression re = buildSearchRegex(term, caseSensitive);
    if (!re.isValid())
        return replacement;

    const QRegularExpressionMatch match = re.match(selectedText);
    if (!match.hasMatch())
        return replacement;

    QString result = replacement;
    for (int i = match.lastCapturedIndex(); i >= 1; --i)
        result.replace(QString("\\%1").arg(i), match.captured(i));
    result.replace("\\0", match.captured(0));
    return result;
}

int CodeEditor::findNext(const QString& term, bool caseSensitive, bool regex) {
    d_ptr->m_lastSearchTerm = term;
    d_ptr->m_lastSearchCaseSensitive = caseSensitive;
    d_ptr->m_lastSearchRegex = regex;

    QTextDocument::FindFlags flags;
    if (caseSensitive) flags |= QTextDocument::FindCaseSensitively;
    auto mkRE = [&]{ return buildSearchRegex(term, caseSensitive); };
    QTextCursor cur = d_ptr->m_editor->textCursor();
    QTextCursor match = regex ? d_ptr->m_editor->document()->find(mkRE(), cur)
                              : d_ptr->m_editor->document()->find(term, cur, flags);
    if (match.isNull()) {
        cur.movePosition(QTextCursor::Start);
        match = regex ? d_ptr->m_editor->document()->find(mkRE(), cur)
                      : d_ptr->m_editor->document()->find(term, cur, flags);
    }
    if (d_ptr->m_largeDocumentMode) {
        d_ptr->m_searchSelections.clear();
    } else {
        highlightMatches(d_ptr->m_editor->document(), term, caseSensitive, regex,
                         d_ptr->m_searchSelections, d_ptr->m_theme);
    }
    d_ptr->updateCurrentLineHighlight();
    if (!match.isNull()) { d_ptr->m_editor->setTextCursor(match); d_ptr->m_editor->centerCursor(); return match.selectionStart(); }
    return -1;
}

int CodeEditor::findPrev(const QString& term, bool caseSensitive, bool regex) {
    d_ptr->m_lastSearchTerm = term;
    d_ptr->m_lastSearchCaseSensitive = caseSensitive;
    d_ptr->m_lastSearchRegex = regex;

    QTextDocument::FindFlags flags = QTextDocument::FindBackward;
    if (caseSensitive) flags |= QTextDocument::FindCaseSensitively;
    auto mkRE = [&]{ return buildSearchRegex(term, caseSensitive); };
    QTextCursor cur = d_ptr->m_editor->textCursor();
    QTextCursor match = regex ? d_ptr->m_editor->document()->find(mkRE(), cur, QTextDocument::FindBackward)
                              : d_ptr->m_editor->document()->find(term, cur, flags);
    if (match.isNull()) {
        cur.movePosition(QTextCursor::End);
        match = regex ? d_ptr->m_editor->document()->find(mkRE(), cur, QTextDocument::FindBackward)
                      : d_ptr->m_editor->document()->find(term, cur, flags);
    }
    if (d_ptr->m_largeDocumentMode) {
        d_ptr->m_searchSelections.clear();
    } else {
        highlightMatches(d_ptr->m_editor->document(), term, caseSensitive, regex,
                         d_ptr->m_searchSelections, d_ptr->m_theme);
    }
    d_ptr->updateCurrentLineHighlight();
    if (!match.isNull()) { d_ptr->m_editor->setTextCursor(match); d_ptr->m_editor->centerCursor(); return match.selectionStart(); }
    return -1;
}

void CodeEditor::replaceNext(const QString& term, const QString& replacement) {
    const bool reuseLastOptions = d_ptr->m_lastSearchTerm == term;
    const bool caseSensitive = reuseLastOptions ? d_ptr->m_lastSearchCaseSensitive : false;
    const bool regex = reuseLastOptions ? d_ptr->m_lastSearchRegex : false;

    QTextCursor cursor = d_ptr->m_editor->textCursor();
    if (cursor.hasSelection()
        && selectionMatchesTerm(cursor.selectedText(), term, caseSensitive, regex)) {
        cursor.insertText(buildReplacementText(
            cursor.selectedText(), term, replacement, caseSensitive, regex));
    }

    findNext(term, caseSensitive, regex);
}

void CodeEditor::replaceAll(const QString& term, const QString& replacement) {
    const bool reuseLastOptions = d_ptr->m_lastSearchTerm == term;
    const bool caseSensitive = reuseLastOptions ? d_ptr->m_lastSearchCaseSensitive : false;
    const bool regex = reuseLastOptions ? d_ptr->m_lastSearchRegex : false;

    QTextCursor cur(d_ptr->m_editor->document());
    QTextDocument::FindFlags flags;
    if (caseSensitive)
        flags |= QTextDocument::FindCaseSensitively;
    const QRegularExpression re = buildSearchRegex(term, caseSensitive);

    cur.beginEditBlock();
    while (true) {
        cur = regex ? d_ptr->m_editor->document()->find(re, cur)
                    : d_ptr->m_editor->document()->find(term, cur, flags);
        if (cur.isNull())
            break;

        const QString selectedText = cur.selectedText();
        cur.insertText(buildReplacementText(
            selectedText, term, replacement, caseSensitive, regex));
    }
    cur.endEditBlock();
    findNext(term, caseSensitive, regex);
}

void CodeEditor::goToLine(int line) {
    if (d_ptr->m_largeFileMode) {
        const qint64 lineByte = d_ptr->largeFileByteForLine(line);
        if (lineByte >= 0)
            d_ptr->requestLargeFileWindow(lineByte, LargeFileAnchorCenter);
        return;
    }

    QTextBlock block = d_ptr->m_editor->document()->findBlockByNumber(qMax(0, line-1));
    if (block.isValid()) {
        QTextCursor cursor(block);
        d_ptr->m_editor->setTextCursor(cursor);
        d_ptr->m_editor->centerCursor();
    }
}

int     CodeEditor::currentLine()   const { return d_ptr->largeFileCurrentLine(); }
int     CodeEditor::currentColumn() const { return d_ptr->m_editor->textCursor().columnNumber() + 1; }
QString CodeEditor::selectedText()  const { return d_ptr->m_editor->textCursor().selectedText(); }
void    CodeEditor::selectAll()           { d_ptr->m_editor->selectAll(); }

void CodeEditor::setCustomKeywords(const QStringList& kw) { if (d_ptr->m_completer) d_ptr->m_completer->setCustomKeywords(kw); }
void CodeEditor::addCustomKeyword (const QString& kw)     { if (d_ptr->m_completer) d_ptr->m_completer->addCustomKeyword(kw); }
void CodeEditor::setReadOnly(bool r) {
    d_ptr->m_savedReadOnly = r;
    if (!d_ptr->m_largeFileMode)
        d_ptr->m_editor->setReadOnly(r);
}
bool CodeEditor::isReadOnly() const  { return d_ptr->m_editor->isReadOnly(); }

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    d_ptr->updateLineNumberAreaWidth(0);
}

// ── Function list popup ───────────────────────────────────────────────────────

void CodeEditor::showFunctionList()
{
    if (d_ptr->m_largeFileMode || d_ptr->m_largeDocumentMode)
        return;

    if (d_ptr->m_functionPopup && d_ptr->m_functionPopup->isEmpty())
        d_ptr->updateFunctionList();
    if (d_ptr->m_functionPopup)
        d_ptr->m_functionPopup->showBelowWidget(this);
}

QVector<CodeEditor::FunctionInfo> CodeEditor::getFunctionList() const
{
    if (d_ptr->m_largeFileMode || d_ptr->m_largeDocumentMode)
        return {};

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
