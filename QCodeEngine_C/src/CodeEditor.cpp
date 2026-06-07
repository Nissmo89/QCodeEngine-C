#include "CodeEditor/CodeEditor.h"
#include "CodeEditor/diagnosticmanager.h"
#include "CodeEditor_p.h"
#include <QHBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QVector>
#include <QSet>
#include <QTimer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QThread>
#include <QToolTip>
#include <memory>
#include <algorithm>
#include <utility>
#include "TreeSitterQuery_C.h"
#include "LiveIndentController.h"
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

QString documentSlice(const QTextDocument* doc, int start, int end)
{
    if (!doc)
        return {};

    const int maxPos = qMax(0, doc->characterCount() - 1);
    start = qBound(0, start, maxPos);
    end = qBound(0, end, maxPos);
    if (end <= start)
        return {};

    QTextCursor cursor(const_cast<QTextDocument*>(doc));
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

struct FoldChipVisual {
    QString label;
    QRect rect;
};

static QString foldedChipLabelForHiddenLines(int hiddenLines)
{
    if (hiddenLines <= 0)
        return QStringLiteral("+ folded");
    if (hiddenLines == 1)
        return QStringLiteral("+ 1 line");
    return QStringLiteral("+ %1 lines").arg(hiddenLines);
}

static QString foldedChipTooltipForHiddenLines(int hiddenLines)
{
    if (hiddenLines <= 0)
        return QStringLiteral("Click to expand folded block");
    if (hiddenLines == 1)
        return QStringLiteral("Click to expand 1 hidden line");
    return QStringLiteral("Click to expand %1 hidden lines").arg(hiddenLines);
}

static FoldChipVisual buildFoldChipVisual(const QString& blockText,
                                          int hiddenLines,
                                          const QFontMetrics& fm,
                                          int tabWidth,
                                          int viewportWidth,
                                          int contentOffsetX,
                                          int blockTop,
                                          int blockHeight)
{
    FoldChipVisual out;
    out.label = foldedChipLabelForHiddenLines(hiddenLines);

    QString expandedText = blockText;
    expandedText.replace(QLatin1Char('\t'), QString(tabWidth, QLatin1Char(' ')));

    const int textWidth = fm.horizontalAdvance(expandedText);
    const int padX = 8;
    const int padY = 3;
    const int chipW = fm.horizontalAdvance(out.label) + padX * 2;
    const int chipH = qMax(fm.height() + padY * 2, blockHeight - 8);

    int chipX = contentOffsetX + textWidth + 10;
    const int maxX = qMax(6, viewportWidth - chipW - 6);
    chipX = qBound(6, chipX, maxX);

    const int chipY = blockTop + qMax(0, (blockHeight - chipH) / 2);
    out.rect = QRect(chipX, chipY, chipW, chipH);
    return out;
}

static QColor bracketPairGuideColor(const QEditorTheme& theme, int depth, bool active)
{
    depth = qMax(0, depth);
    QColor color;
    if (!theme.rainbowColors.isEmpty())
        color = theme.rainbowColors[depth % theme.rainbowColors.size()];

    const QColor fallback[] = {
        Qt::red,
        QColor(255, 165, 0),
        Qt::yellow,
        Qt::green,
        Qt::cyan,
        Qt::magenta
    };
    if (!color.isValid())
        color = fallback[depth % 6];

    color.setAlpha(active ? 235 : 130);
    return color;
}

static int textPrefixPixelWidth(const QString& text, const QFontMetrics& fm, int tabWidth)
{
    const int safeTabWidth = qMax(1, tabWidth);
    const int spaceWidth = qMax(1, fm.horizontalAdvance(QLatin1Char(' ')));
    int width = 0;
    int column = 0;

    for (QChar ch : text) {
        if (ch == QLatin1Char('\t')) {
            const int spaces = safeTabWidth - (column % safeTabWidth);
            width += spaces * spaceWidth;
            column += spaces;
            continue;
        }

        width += fm.horizontalAdvance(ch);
        ++column;
    }

    return width;
}

static qreal characterCenterPixelX(const QString& text,
                                   int column,
                                   const QFontMetrics& fm,
                                   int tabWidth,
                                   int contentOffsetX)
{
    if (column < 0 || column >= text.size())
        return contentOffsetX;

    return contentOffsetX
           + textPrefixPixelWidth(text.left(column), fm, tabWidth)
           + qMax<qreal>(1.0, fm.horizontalAdvance(text.at(column)) * 0.5);
}

static bool visiblyDifferent(qreal a, qreal b)
{
    return a > b + 0.5 || a < b - 0.5;
}

struct ActiveBraceFold {
    int startBlock = -1;
    int endBlock = -1;
    int openColumn = -1;
    int closeColumn = -1;
    int depth = 0;

    bool isValid() const
    {
        return startBlock >= 0
               && endBlock > startBlock
               && openColumn >= 0
               && closeColumn >= 0;
    }
};

static int firstNonWhitespaceColumn(const QString& text)
{
    for (int i = 0; i < text.size(); ++i) {
        if (!text.at(i).isSpace())
            return i;
    }
    return -1;
}

static bool findFoldGuidePrefix(const QTextDocument* doc,
                                int startBlockNumber,
                                int endBlockNumber,
                                QString* outPrefix)
{
    if (outPrefix)
        outPrefix->clear();

    if (!doc || startBlockNumber > endBlockNumber)
        return false;

    for (int blockNumber = startBlockNumber; blockNumber <= endBlockNumber; ++blockNumber) {
        const QTextBlock block = doc->findBlockByNumber(blockNumber);
        if (!block.isValid())
            break;

        const int column = firstNonWhitespaceColumn(block.text());
        if (column >= 0) {
            if (outPrefix)
                *outPrefix = block.text().left(column);
            return true;
        }
    }

    return false;
}

static bool resolveBraceColumnsForFold(const QTextDocument* doc,
                                       int startBlockNumber,
                                       int endBlockNumber,
                                       int* outOpenColumn,
                                       int* outCloseColumn)
{
    if (outOpenColumn)
        *outOpenColumn = -1;
    if (outCloseColumn)
        *outCloseColumn = -1;

    if (!doc || startBlockNumber < 0 || endBlockNumber <= startBlockNumber)
        return false;

    const QTextBlock openBlock = doc->findBlockByNumber(startBlockNumber);
    const QTextBlock closeBlock = doc->findBlockByNumber(endBlockNumber);
    if (!openBlock.isValid() || !closeBlock.isValid())
        return false;

    const int sliceStart = openBlock.position();
    const int sliceEnd = closeBlock.position() + closeBlock.text().size();
    const QString text = documentSlice(doc, sliceStart, sliceEnd);
    if (text.isEmpty())
        return false;

    QVector<bool> mask;
    buildBracketCountableMask(text, mask);

    const int firstLineEnd = text.indexOf(QLatin1Char('\n'));
    const int searchEnd = (firstLineEnd >= 0) ? firstLineEnd : text.size();
    for (int i = 0; i < searchEnd; ++i) {
        if (!mask.at(i) || text.at(i) != QLatin1Char('{'))
            continue;

        const int closePos = findClosingPartner(text, mask, i);
        if (closePos < 0)
            continue;

        const QTextBlock resolvedOpenBlock = doc->findBlock(sliceStart + i);
        const QTextBlock resolvedCloseBlock = doc->findBlock(sliceStart + closePos);
        if (!resolvedOpenBlock.isValid() || !resolvedCloseBlock.isValid())
            continue;
        if (resolvedOpenBlock.blockNumber() != startBlockNumber
            || resolvedCloseBlock.blockNumber() != endBlockNumber) {
            continue;
        }

        if (outOpenColumn)
            *outOpenColumn = sliceStart + i - resolvedOpenBlock.position();
        if (outCloseColumn)
            *outCloseColumn = sliceStart + closePos - resolvedCloseBlock.position();
        return true;
    }

    return false;
}

static ActiveBraceFold findActiveBraceFold(const QTextDocument* doc,
                                           const QMap<int, int>& foldRanges,
                                           int cursorBlockNumber)
{
    QVector<ActiveBraceFold> containingFolds;

    for (auto it = foldRanges.constBegin(); it != foldRanges.constEnd(); ++it) {
        const int startBlock = it.key();
        if (startBlock > cursorBlockNumber)
            break;

        const int endBlock = it.value();
        if (cursorBlockNumber < startBlock || cursorBlockNumber > endBlock)
            continue;

        int openColumn = -1;
        int closeColumn = -1;
        if (!resolveBraceColumnsForFold(doc, startBlock, endBlock, &openColumn, &closeColumn))
            continue;

        containingFolds.append({startBlock, endBlock, openColumn, closeColumn, 0});
    }

    if (containingFolds.isEmpty())
        return {};

    ActiveBraceFold activeFold;
    int activeSpan = -1;
    for (const ActiveBraceFold& fold : std::as_const(containingFolds)) {
        const int span = fold.endBlock - fold.startBlock;
        if (activeSpan < 0 || span < activeSpan) {
            activeSpan = span;
            activeFold = fold;
        }
    }

    int depth = 0;
    for (const ActiveBraceFold& fold : std::as_const(containingFolds)) {
        if (fold.startBlock <= activeFold.startBlock && fold.endBlock >= activeFold.endBlock)
            ++depth;
    }
    activeFold.depth = qMax(0, depth - 1);
    return activeFold;
}

static bool sameBracketGuideState(const BracketGuideState& a, const BracketGuideState& b)
{
    return a.startBlock == b.startBlock
           && a.endBlock == b.endBlock
           && a.openColumn == b.openColumn
           && a.closeColumn == b.closeColumn
           && a.depth == b.depth
           && a.prefix == b.prefix;
}

static BracketGuideState buildBracketGuideState(const QTextDocument* doc,
                                                const QMap<int, int>& foldRanges,
                                                int cursorBlockNumber)
{
    BracketGuideState state;
    const ActiveBraceFold activeFold = findActiveBraceFold(doc, foldRanges, cursorBlockNumber);
    if (!activeFold.isValid())
        return state;

    state.startBlock = activeFold.startBlock;
    state.endBlock = activeFold.endBlock;
    state.openColumn = activeFold.openColumn;
    state.closeColumn = activeFold.closeColumn;
    state.depth = activeFold.depth;

    if (!findFoldGuidePrefix(doc, activeFold.startBlock + 1, activeFold.endBlock - 1, &state.prefix)) {
        const QTextBlock closeBlock = doc ? doc->findBlockByNumber(activeFold.endBlock) : QTextBlock();
        if (closeBlock.isValid()) {
            const int safeCloseColumn = qBound(0, activeFold.closeColumn, closeBlock.text().size());
            state.prefix = closeBlock.text().left(safeCloseColumn);
        }
    }

    return state;
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
    fmap["keyword"]               = makeFormat(theme.tokenKeyword); //theme.keywordBold
    fmap["keyword.control"]       = makeFormat(theme.tokenKeywordControl); //theme.keywordBold
    fmap["keyword.preproc"]       = makeFormat(theme.tokenKeywordPreproc); //theme.keywordBold
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
    fmap["function"]              = makeFormat(theme.tokenFunction); //theme.functionBold
    fmap["function.special"]      = makeFormat(theme.tokenKeywordPreproc); //theme.functionBold
    fmap["type"]                  = makeFormat(theme.tokenType); // theme.typeBold
    fmap["property"]              = makeFormat(theme.tokenField);
    fmap["label"]                 = makeFormat(theme.tokenLabel);
    fmap["attribute"]             = makeFormat(theme.tokenAttribute);
    { QTextCharFormat fb; fb.setForeground(theme.foreground); fmap[""] = fb; }
    return fmap;
}

static constexpr int kMinEditorPointSize = 8;
static constexpr int kMaxEditorPointSize = 40;

static int clampEditorPointSize(int pointSize)
{
    return qBound(kMinEditorPointSize, pointSize, kMaxEditorPointSize);
}

static int editorPointSize(const QFont& font)
{
    const int pointSize = QFontInfo(font).pointSize();
    return pointSize > 0 ? pointSize : 14;
}

static int editorLineHeight(const QFont& font)
{
    return qMax(18, qRound(QFontMetricsF(font).lineSpacing() * 1.2));
}

static void applyEditorStyle(QPlainTextEdit* editor) {
    const int lineHeightPx = editorLineHeight(editor->font());
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
    : QPlainTextEdit(parent), d_ptr(d)
{
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
}

void InnerEditor::keyPressEvent(QKeyEvent* e) {
    if (d_ptr->dispatchPluginKeyPress(e)) return;
    if (d_ptr->handleMultiCursorEdit(e)) return;
    if (d_ptr->m_completer && d_ptr->m_completer->handleKeyPress(e)) return;
    if (d_ptr->handleKeyPress(e)) return;
    QPlainTextEdit::keyPressEvent(e);
    if (d_ptr->m_completer && !e->text().isEmpty())
        d_ptr->m_completer->updatePopup();
}

bool InnerEditor::foldedChipAt(const QPoint& pos, int* foldStart, QRect* chipRect, int* hiddenLines) const
{
    if (!d_ptr || !d_ptr->m_foldingEnabled || !d_ptr->m_foldManager)
        return false;

    const QMap<int, int>& foldRanges = d_ptr->m_foldManager->foldRanges();
    if (foldRanges.isEmpty())
        return false;

    QFontMetrics fm(font());
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    qreal bottom = top + blockBoundingRect(block).height();
    const int viewportWidth = viewport()->width();
    const int contentX = static_cast<int>(contentOffset().x());

    while (block.isValid() && top <= viewport()->height()) {
        if (block.isVisible() && bottom >= 0 && d_ptr->m_foldManager->isFolded(blockNumber)) {
            const auto it = foldRanges.constFind(blockNumber);
            if (it != foldRanges.constEnd()) {
                const int hidden = qMax(0, it.value() - blockNumber - 1);
                const int blockTop = static_cast<int>(top);
                const int blockHeight = qMax(1, static_cast<int>(bottom - top));
                const FoldChipVisual chip = buildFoldChipVisual(
                    block.text(), hidden, fm, d_ptr->m_tabWidth,
                    viewportWidth, contentX, blockTop, blockHeight);
                if (chip.rect.contains(pos)) {
                    if (foldStart) *foldStart = blockNumber;
                    if (chipRect) *chipRect = chip.rect;
                    if (hiddenLines) *hiddenLines = hidden;
                    return true;
                }
            }
        }

        block = block.next();
        top = bottom;
        bottom = top + blockBoundingRect(block).height();
        ++blockNumber;
    }

    return false;
}

void InnerEditor::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && !(e->modifiers() & Qt::AltModifier)
        && d_ptr->m_foldingEnabled && d_ptr->m_foldManager) {
        int foldStart = -1;
        if (foldedChipAt(e->position().toPoint(), &foldStart, nullptr, nullptr) && foldStart >= 0) {
            d_ptr->m_foldManager->toggleFold(foldStart);
            m_hoveredFoldStart = -1;
            QToolTip::hideText();
            viewport()->unsetCursor();
            e->accept();
            return;
        }
    }

    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier)) {
        const int pos = cursorForPosition(e->position().toPoint()).position();
        d_ptr->addExtraCursorAtPosition(pos, true);
        d_ptr->updateCurrentLineHighlight();
        viewport()->update();
        e->accept();
        return;
    }

    if (!(e->modifiers() & Qt::AltModifier))
        d_ptr->clearExtraCursors();

    QPlainTextEdit::mousePressEvent(e);
}

void InnerEditor::mouseMoveEvent(QMouseEvent* e)
{
    QPlainTextEdit::mouseMoveEvent(e);

    if (e->buttons() != Qt::NoButton) {
        if (m_hoveredFoldStart != -1) {
            m_hoveredFoldStart = -1;
            viewport()->update();
        }
        QToolTip::hideText();
        viewport()->unsetCursor();
        return;
    }

    int foldStart = -1;
    int hiddenLines = 0;
    QRect chipRect;
    const bool hoveringChip = !(e->modifiers() & Qt::AltModifier)
                           && foldedChipAt(e->position().toPoint(), &foldStart, &chipRect, &hiddenLines);

    const int newHover = hoveringChip ? foldStart : -1;
    if (newHover != m_hoveredFoldStart) {
        m_hoveredFoldStart = newHover;
        viewport()->update();
    }

    if (hoveringChip) {
        viewport()->setCursor(Qt::PointingHandCursor);
        QToolTip::showText(e->globalPosition().toPoint(),
                           foldedChipTooltipForHiddenLines(hiddenLines),
                           viewport(), chipRect);
    } else {
        viewport()->unsetCursor();
        QToolTip::hideText();
    }
}

void InnerEditor::leaveEvent(QEvent* e)
{
    if (m_hoveredFoldStart != -1) {
        m_hoveredFoldStart = -1;
        viewport()->update();
    }
    viewport()->unsetCursor();
    QToolTip::hideText();
    QPlainTextEdit::leaveEvent(e);
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

    QPainter painter(viewport());
    painter.setFont(font());
    QFontMetrics fm(font());

    if (d_ptr->m_bracketPairGuidesEnabled
        && !d_ptr->m_largeDocumentMode
        && d_ptr->m_foldManager) {
        struct VisibleBlockSpan {
            int blockNumber = -1;
            qreal top = 0;
            qreal bottom = 0;
        };

        QVector<VisibleBlockSpan> visibleBlocks;
        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
        qreal bottom = top + blockBoundingRect(block).height();

        while (block.isValid() && top <= e->rect().bottom()) {
            if (block.isVisible() && bottom >= e->rect().top())
                visibleBlocks.append({blockNumber, top, bottom});

            block = block.next();
            top = bottom;
            bottom = top + blockBoundingRect(block).height();
            ++blockNumber;
        }

        if (!visibleBlocks.isEmpty()) {
            auto lineCenterY = [](const VisibleBlockSpan& span) {
                // return (span.top + span.bottom) * 0.5;
                return span.bottom;
            };

            auto findVisibleIndex = [&visibleBlocks](int blockNumber) {
                for (int i = 0; i < visibleBlocks.size(); ++i) {
                    if (visibleBlocks.at(i).blockNumber == blockNumber)
                        return i;
                }
                return -1;
            };

            const int contentX = static_cast<int>(contentOffset().x());
            const qreal cellCenterOffset = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char(' ')) * 0.5);
            const qreal verticalInset = 3.0;
            const int firstVisibleBlock = visibleBlocks.first().blockNumber;
            const int lastVisibleBlock = visibleBlocks.last().blockNumber;
            const BracketGuideState& activeFold = d_ptr->m_activeBracketGuide;
            const bool activeFoldVisible =
                activeFold.isValid()
                && activeFold.endBlock >= firstVisibleBlock
                && activeFold.startBlock <= lastVisibleBlock
                && !d_ptr->m_foldManager->isFolded(activeFold.startBlock);
            if (activeFoldVisible) {
                const QTextBlock openBlock = document()->findBlockByNumber(activeFold.startBlock);
                const QTextBlock closeBlock = document()->findBlockByNumber(activeFold.endBlock);

                if (openBlock.isValid() && closeBlock.isValid()) {
                    const int headerVisibleIndex = findVisibleIndex(activeFold.startBlock);
                    const int closeVisibleIndex = findVisibleIndex(activeFold.endBlock);

                    int firstVisibleInnerIndex = -1;
                    int lastVisibleBeforeCloseIndex = -1;
                    for (int i = 0; i < visibleBlocks.size(); ++i) {
                        const int visibleBlockNumber = visibleBlocks.at(i).blockNumber;
                        if (visibleBlockNumber > activeFold.startBlock
                            && visibleBlockNumber < activeFold.endBlock) {
                            if (firstVisibleInnerIndex < 0)
                                firstVisibleInnerIndex = i;
                            lastVisibleBeforeCloseIndex = i;
                        }
                    }

                    const qreal openX = characterCenterPixelX(
                        openBlock.text(), activeFold.openColumn, fm, d_ptr->m_tabWidth, contentX);
                    const qreal closeX = characterCenterPixelX(
                        closeBlock.text(), activeFold.closeColumn, fm, d_ptr->m_tabWidth, contentX);
                    const qreal rawGuideX = contentX
                                            + textPrefixPixelWidth(
                                                  activeFold.prefix, fm, d_ptr->m_tabWidth)
                                            + cellCenterOffset;
                    const qreal guideX = qMin(qMin(openX, rawGuideX), closeX);

                    const bool drawHeaderSegment =
                        headerVisibleIndex >= 0 && visiblyDifferent(openX, guideX);
                    const bool drawCloseSegment =
                        closeVisibleIndex >= 0 && visiblyDifferent(closeX, guideX);

                    qreal startY = 0.0;
                    bool hasStartY = false;
                    if (headerVisibleIndex >= 0) {
                        startY = lineCenterY(visibleBlocks.at(headerVisibleIndex));
                        hasStartY = true;
                    } else if (firstVisibleInnerIndex >= 0) {
                        startY = visibleBlocks.at(firstVisibleInnerIndex).top + verticalInset;
                        hasStartY = true;
                    } else if (headerVisibleIndex < 0 && drawCloseSegment) {
                        startY = visibleBlocks.at(closeVisibleIndex).top + verticalInset;
                        hasStartY = true;
                    }

                    qreal endY = -1.0;
                    bool hasEndY = false;
                    if (drawCloseSegment) {
                        endY = lineCenterY(visibleBlocks.at(closeVisibleIndex));
                        hasEndY = true;
                    } else if (closeVisibleIndex >= 0) {
                        endY = visibleBlocks.at(closeVisibleIndex).top - verticalInset;
                        hasEndY = true;
                    } else if (lastVisibleBeforeCloseIndex >= 0) {
                        endY = visibleBlocks.at(lastVisibleBeforeCloseIndex).bottom - verticalInset;
                        hasEndY = true;
                    } else if (drawHeaderSegment) {
                        endY = visibleBlocks.at(headerVisibleIndex).bottom - verticalInset;
                        hasEndY = true;
                    }

                    painter.save();
                    painter.setRenderHint(QPainter::Antialiasing, true);
                    painter.setPen(QPen(bracketPairGuideColor(d_ptr->m_theme, activeFold.depth, true),
                                        2.0,
                                        Qt::SolidLine,
                                        Qt::SquareCap,
                                        Qt::MiterJoin));

                    QPainterPath guidePath;
                    bool hasPath = false;
                    qreal currentX = 0.0;
                    qreal currentY = 0.0;

                    auto moveTo = [&guidePath, &hasPath, &currentX, &currentY](qreal x, qreal y) {
                        guidePath.moveTo(x, y);
                        hasPath = true;
                        currentX = x;
                        currentY = y;
                    };

                    auto lineTo = [&guidePath, &hasPath, &currentX, &currentY](qreal x, qreal y) {
                        if (!hasPath) {
                            guidePath.moveTo(x, y);
                            hasPath = true;
                        } else if (visiblyDifferent(currentX, x) || visiblyDifferent(currentY, y)) {
                            guidePath.lineTo(x, y);
                        }
                        currentX = x;
                        currentY = y;
                    };

                    if (drawHeaderSegment) {
                        const qreal headerY = lineCenterY(visibleBlocks.at(headerVisibleIndex));
                        moveTo(openX, headerY);
                        lineTo(guideX, headerY);
                    } else if (hasStartY) {
                        moveTo(guideX, startY);
                    }

                    if (hasStartY && hasEndY && endY > startY) {
                        if (!hasPath)
                            moveTo(guideX, startY);
                        lineTo(guideX, endY);
                    }

                    if (drawCloseSegment) {
                        const qreal closeY = lineCenterY(visibleBlocks.at(closeVisibleIndex));
                        if (!hasPath)
                            moveTo(guideX, closeY);
                        else
                            lineTo(guideX, closeY);
                        lineTo(closeX, closeY);
                    }

                    if (hasPath)
                        painter.drawPath(guidePath);

                    painter.restore();
                }
            }
        }
    }

    if (d_ptr->m_foldingEnabled && d_ptr->m_foldManager) {
        const QMap<int, int>& foldRanges = d_ptr->m_foldManager->foldRanges();
        QTextBlock block = firstVisibleBlock();
        int  blockNumber = block.blockNumber();
        qreal top    = blockBoundingGeometry(block).translated(contentOffset()).top();
        qreal bottom = top + blockBoundingRect(block).height();
        const int viewportWidth = viewport()->width();
        const int contentX = static_cast<int>(contentOffset().x());

        while (block.isValid() && top <= e->rect().bottom()) {
            if (block.isVisible() && bottom >= e->rect().top()) {
                if (d_ptr->m_foldManager->isFolded(blockNumber)) {
                    const auto it = foldRanges.constFind(blockNumber);
                    if (it != foldRanges.constEnd()) {
                        const int hiddenLines = qMax(0, it.value() - blockNumber - 1);
                        const int blockTop = static_cast<int>(top);
                        const int blockHeight = qMax(1, static_cast<int>(bottom - top));
                        const FoldChipVisual chip = buildFoldChipVisual(
                            block.text(), hiddenLines, fm, d_ptr->m_tabWidth,
                            viewportWidth, contentX, blockTop, blockHeight);
                        const bool hovered = (m_hoveredFoldStart == blockNumber);

                        QColor chipBg = d_ptr->m_theme.tokenComment.isValid()
                                      ? d_ptr->m_theme.tokenComment
                                      : d_ptr->m_theme.foreground;
                        chipBg.setAlpha(hovered ? 95 : 56);

                        QColor chipBorder = d_ptr->m_theme.accent.isValid()
                                          ? d_ptr->m_theme.accent
                                          : d_ptr->m_theme.tokenComment;
                        if (!chipBorder.isValid())
                            chipBorder = d_ptr->m_theme.foreground;
                        chipBorder.setAlpha(hovered ? 220 : 150);

                        QColor chipText = d_ptr->m_theme.foreground.isValid()
                                        ? d_ptr->m_theme.foreground
                                        : QColor(230, 230, 230);
                        chipText.setAlpha(hovered ? 245 : 220);

                        painter.save();
                        painter.setRenderHint(QPainter::Antialiasing, true);
                        painter.setPen(QPen(chipBorder, 1));
                        painter.setBrush(chipBg);
                        painter.drawRoundedRect(chip.rect, 6, 6);
                        painter.setPen(chipText);
                        painter.drawText(chip.rect.adjusted(8, 0, -8, 0),
                                         Qt::AlignLeft | Qt::AlignVCenter,
                                         chip.label);
                        painter.restore();
                    }
                }
            }
            block = block.next();
            top   = bottom;
            bottom = top + blockBoundingRect(block).height();
            ++blockNumber;
        }
    }

    if (!d_ptr->m_multiCursors.isEmpty()) {
        painter.save();
        QPen caretPen(d_ptr->m_theme.accent.isValid() ? d_ptr->m_theme.accent : QColor(90, 170, 255));
        caretPen.setWidth(2);
        painter.setPen(caretPen);

        for (const MultiCursorState& mc : d_ptr->m_multiCursors) {
            QTextCursor c(document());
            const int maxPos = qMax(0, document()->characterCount() - 1);
            c.setPosition(qBound(0, mc.position, maxPos));
            const QRect r = cursorRect(c);
            painter.drawLine(r.left(), r.top() + 1, r.left(), r.bottom() - 1);
        }

        painter.restore();
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
        syncMiniMapVisibility();
    });
}

CodeEditorPrivate::~CodeEditorPrivate()
{
    delete m_liveIndentController;
    m_liveIndentController = nullptr;
}

// ── Layout ────────────────────────────────────────────────────────────────────
void CodeEditorPrivate::setupLayout()
{
    m_gutter = new GutterWidget(m_editor, q_ptr);
    m_miniMap = new MiniMapWidget(m_editor, q_ptr);
    m_miniMap->setTheme(m_theme);
    m_miniMap->setVisible(false);

    QHBoxLayout* layout = new QHBoxLayout(q_ptr);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_gutter);
    layout->addWidget(m_editor);
    layout->addWidget(m_miniMap);
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

    m_liveIndentController = new LiveIndentController(m_editor, m_highlighter);
    m_liveIndentController->setEnabled(m_autoIndent);
    m_liveIndentController->setTabWidth(m_tabWidth);
    m_liveIndentController->setInsertSpaces(m_insertSpaces);
    m_liveIndentController->setAutoBracketEnabled(m_autoBracket);
    m_liveIndentController->setStylePreset(m_indentStylePreset);

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

    QAction* zoomInAction = new QAction(q_ptr);
    zoomInAction->setShortcuts(QList<QKeySequence>{
        QKeySequence(Qt::CTRL | Qt::Key_Plus),
        QKeySequence(Qt::CTRL | Qt::Key_Equal)
    });
    connect(zoomInAction, &QAction::triggered, this, [this]() {
        adjustZoom(1);
    });
    q_ptr->addAction(zoomInAction);

    QAction* zoomOutAction = new QAction(q_ptr);
    zoomOutAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    connect(zoomOutAction, &QAction::triggered, this, [this]() {
        adjustZoom(-1);
    });
    q_ptr->addAction(zoomOutAction);
}

void CodeEditorPrivate::adjustZoom(int delta)
{
    if (delta == 0)
        return;

    QFont font = m_editor->font();
    const int currentPointSize = clampEditorPointSize(editorPointSize(font));
    const int nextPointSize = clampEditorPointSize(currentPointSize + delta);
    if (nextPointSize == currentPointSize)
        return;

    m_zoomPointSize = nextPointSize;
    m_theme.fontFamily = font.family();
    m_theme.fontSize = nextPointSize;
    q_ptr->setTheme(m_theme);
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

    if (m_foldingEnabled || m_bracketPairGuidesEnabled)
        m_foldManager->updateFoldRanges(treePtr, m_editor->document());

    updateActiveBracketGuide();

    m_lineHighlighter->updateFromTree(treePtr, m_editor->document());

    m_syntaxChecker->analyze(treePtr);
}

void CodeEditorPrivate::updateActiveBracketGuide(bool forceRepaint)
{
    BracketGuideState nextState;
    if (m_bracketPairGuidesEnabled
        && !m_largeDocumentMode
        && m_editor
        && m_foldManager) {
        nextState = buildBracketGuideState(
            m_editor->document(),
            m_foldManager->foldRanges(),
            m_editor->textCursor().blockNumber());
    }

    const bool changed = !sameBracketGuideState(m_activeBracketGuide, nextState);
    if (changed)
        m_activeBracketGuide = nextState;

    if ((forceRepaint || changed) && m_editor)
        m_editor->viewport()->update();
}

void CodeEditorPrivate::syncMiniMapVisibility()
{
    if (!m_miniMap)
        return;

    const bool visible = m_miniMapVisibleRequested
                         && !m_largeFileMode
                         && !m_asyncLoadInProgress;
    m_miniMap->setVisible(visible);
    if (visible)
        m_miniMap->update();
}

bool CodeEditorPrivate::dispatchPluginKeyPress(QKeyEvent* event)
{
    for (auto it = m_plugins.cbegin(); it != m_plugins.cend(); ++it) {
        CodeEditorPlugin* plugin = it.value();
        if (plugin && plugin->onKeyPress(q_ptr, event))
            return true;
    }
    return false;
}

void CodeEditorPrivate::detachPlugins()
{
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        if (it.value())
            it.value()->onDetach(q_ptr);
    }
    m_plugins.clear();
}

bool CodeEditorPrivate::addExtraCursorAtPosition(int position, bool toggleExisting)
{
    if (!m_editor || !m_editor->document())
        return false;

    const int maxPos = qMax(0, m_editor->document()->characterCount() - 1);
    position = qBound(0, position, maxPos);

    QTextCursor primary = m_editor->textCursor();
    if (!primary.hasSelection() && primary.position() == position)
        return false;

    for (int i = 0; i < m_multiCursors.size(); ++i) {
        if (m_multiCursors[i].anchor == position && m_multiCursors[i].position == position) {
            if (toggleExisting) {
                m_multiCursors.removeAt(i);
                return true;
            }
            return false;
        }
    }

    m_multiCursors.append({position, position});
    normalizeExtraCursors();
    return true;
}

void CodeEditorPrivate::clearExtraCursors()
{
    if (m_multiCursors.isEmpty())
        return;
    m_multiCursors.clear();
    updateCurrentLineHighlight();
    m_editor->viewport()->update();
}

void CodeEditorPrivate::normalizeExtraCursors()
{
    if (!m_editor || !m_editor->document())
        return;

    const int maxPos = qMax(0, m_editor->document()->characterCount() - 1);
    const QTextCursor primary = m_editor->textCursor();
    const int pAnchor = primary.anchor();
    const int pPos = primary.position();

    QSet<quint64> seen;
    QVector<MultiCursorState> normalized;
    normalized.reserve(m_multiCursors.size());

    for (MultiCursorState mc : m_multiCursors) {
        mc.anchor = qBound(0, mc.anchor, maxPos);
        mc.position = qBound(0, mc.position, maxPos);

        if (mc.anchor == pAnchor && mc.position == pPos)
            continue;

        const quint64 key =
            (static_cast<quint64>(static_cast<quint32>(mc.anchor)) << 32)
            | static_cast<quint32>(mc.position);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        normalized.append(mc);
    }

    m_multiCursors = normalized;
}

bool CodeEditorPrivate::moveExtraCursorsVertically(int deltaBlocks)
{
    if (!m_editor || !m_editor->document() || deltaBlocks == 0)
        return false;

    const int blockCount = qMax(1, m_editor->document()->blockCount());

    auto cursorTarget = [this, blockCount, deltaBlocks](const QTextCursor& c) {
        const int sourceBlock = c.blockNumber();
        const int targetBlockNo = qBound(0, sourceBlock + deltaBlocks, blockCount - 1);
        const QTextBlock targetBlock = m_editor->document()->findBlockByNumber(targetBlockNo);
        const int targetCol = c.columnNumber();
        const int lineLen = qMax(0, targetBlock.length() - 1);
        return targetBlock.position() + qMin(targetCol, lineLen);
    };

    bool changed = false;
    changed |= addExtraCursorAtPosition(cursorTarget(m_editor->textCursor()), false);

    const QVector<MultiCursorState> existing = m_multiCursors;
    for (const MultiCursorState& mc : existing) {
        QTextCursor c(m_editor->document());
        c.setPosition(mc.anchor);
        c.setPosition(mc.position, QTextCursor::KeepAnchor);
        changed |= addExtraCursorAtPosition(cursorTarget(c), false);
    }

    if (changed) {
        normalizeExtraCursors();
        updateCurrentLineHighlight();
        m_editor->viewport()->update();
    }
    return changed;
}

QList<QTextEdit::ExtraSelection> CodeEditorPrivate::multiCursorSelections() const
{
    QList<QTextEdit::ExtraSelection> out;
    if (!m_editor)
        return out;

    QColor multiSel = m_theme.selectionBackground.isValid()
                          ? m_theme.selectionBackground
                          : QColor(90, 120, 180);
    multiSel.setAlpha(95);

    for (const MultiCursorState& mc : m_multiCursors) {
        if (mc.anchor == mc.position)
            continue;
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(multiSel);
        sel.cursor = QTextCursor(m_editor->document());
        sel.cursor.setPosition(mc.anchor);
        sel.cursor.setPosition(mc.position, QTextCursor::KeepAnchor);
        out.append(sel);
    }

    return out;
}

bool CodeEditorPrivate::handleMultiCursorEdit(QKeyEvent* event)
{
    if (!m_editor || m_multiCursors.isEmpty())
        return false;

    if (event->key() == Qt::Key_Escape) {
        clearExtraCursors();
        return true;
    }

    if (m_editor->isReadOnly())
        return false;

    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool shiftOnly = mods == Qt::ShiftModifier;
    const bool plain = mods == Qt::NoModifier;
    if (!(plain || shiftOnly))
        return false;

    struct CursorEntry {
        QTextCursor cursor;
        bool primary = false;
        int order = 0;
    };

    QVector<CursorEntry> entries;
    entries.reserve(m_multiCursors.size() + 1);
    entries.append({m_editor->textCursor(), true, 0});
    for (int i = 0; i < m_multiCursors.size(); ++i) {
        QTextCursor c(m_editor->document());
        c.setPosition(m_multiCursors[i].anchor);
        c.setPosition(m_multiCursors[i].position, QTextCursor::KeepAnchor);
        entries.append({c, false, i + 1});
    }

    auto commitEntries = [this](QVector<CursorEntry>& data) {
        std::sort(data.begin(), data.end(), [](const CursorEntry& a, const CursorEntry& b) {
            return a.order < b.order;
        });

        for (const CursorEntry& e : std::as_const(data)) {
            if (e.primary) {
                m_editor->setTextCursor(e.cursor);
                break;
            }
        }

        m_multiCursors.clear();
        for (const CursorEntry& e : std::as_const(data)) {
            if (!e.primary)
                m_multiCursors.append({e.cursor.anchor(), e.cursor.position()});
        }
        normalizeExtraCursors();
        updateCurrentLineHighlight();
        m_editor->viewport()->update();
    };

    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right
        || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down
        || event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
        QTextCursor::MoveMode moveMode = shiftOnly
                                             ? QTextCursor::KeepAnchor
                                             : QTextCursor::MoveAnchor;
        for (CursorEntry& e : entries) {
            switch (event->key()) {
            case Qt::Key_Left:  e.cursor.movePosition(QTextCursor::Left, moveMode); break;
            case Qt::Key_Right: e.cursor.movePosition(QTextCursor::Right, moveMode); break;
            case Qt::Key_Up:    e.cursor.movePosition(QTextCursor::Up, moveMode); break;
            case Qt::Key_Down:  e.cursor.movePosition(QTextCursor::Down, moveMode); break;
            case Qt::Key_Home:  e.cursor.movePosition(QTextCursor::StartOfLine, moveMode); break;
            case Qt::Key_End:   e.cursor.movePosition(QTextCursor::EndOfLine, moveMode); break;
            default: break;
            }
        }
        commitEntries(entries);
        return true;
    }

    enum class EditOp {
        InsertText,
        InsertNewline,
        InsertTab,
        Backspace,
        Delete
    };

    EditOp op;
    QString payload;
    if (event->key() == Qt::Key_Backspace) {
        op = EditOp::Backspace;
    } else if (event->key() == Qt::Key_Delete) {
        op = EditOp::Delete;
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        op = EditOp::InsertNewline;
    } else if (event->key() == Qt::Key_Tab) {
        op = EditOp::InsertTab;
    } else if (!event->text().isEmpty() && event->text().at(0).isPrint()) {
        op = EditOp::InsertText;
        payload = event->text();
    } else {
        return false;
    }

    std::sort(entries.begin(), entries.end(), [](const CursorEntry& a, const CursorEntry& b) {
        return qMax(a.cursor.selectionStart(), a.cursor.position())
               > qMax(b.cursor.selectionStart(), b.cursor.position());
    });

    const bool allowSmartIndentMultiCursor =
        m_liveIndentController
        && (op == EditOp::InsertNewline
            || op == EditOp::Backspace
            || (op == EditOp::InsertText
                && payload.size() == 1
                && (payload.at(0) == QLatin1Char('}')
                    || payload.at(0) == QLatin1Char('{'))));

    QTextCursor transaction(m_editor->document());
    transaction.beginEditBlock();
    for (CursorEntry& e : entries) {
        if (allowSmartIndentMultiCursor
            && m_liveIndentController->handleKeyPress(event, e.cursor, false)) {
            continue;
        }

        switch (op) {
        case EditOp::InsertText:
            e.cursor.insertText(payload);
            break;
        case EditOp::InsertNewline:
            e.cursor.insertText("\n");
            break;
        case EditOp::InsertTab:
            e.cursor.insertText(m_insertSpaces ? QString(m_tabWidth, ' ') : QString("\t"));
            break;
        case EditOp::Backspace:
            if (e.cursor.hasSelection())
                e.cursor.removeSelectedText();
            else
                e.cursor.deletePreviousChar();
            break;
        case EditOp::Delete:
            if (e.cursor.hasSelection())
                e.cursor.removeSelectedText();
            else
                e.cursor.deleteChar();
            break;
        }
    }
    transaction.endEditBlock();

    commitEntries(entries);
    return true;
}

// ── Helpers ───────────────────────────────────────────────────────────────────
void CodeEditorPrivate::enforceFixedLineHeight(int from, int charsAdded)
{
    if (charsAdded <= 0) return;

    QTextBlock b   = m_editor->document()->findBlock(from);
    QTextBlock end = m_editor->document()->findBlock(from + charsAdded);
    QTextBlockFormat fmt;
    fmt.setLineHeight(editorLineHeight(m_editor->font()), QTextBlockFormat::FixedHeight);
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
    m_multiCursors.clear();
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
    syncMiniMapVisibility();
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
    syncMiniMapVisibility();
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
    m_loadedFilePath = filePath;
    m_loadedFileSize = fileSize;
    m_savedReadOnly = m_editor->isReadOnly();
    m_multiCursors.clear();
    suspendHeavyEditorFeatures();
    m_editor->setUndoRedoEnabled(false);
    m_editor->setReadOnly(true);
    m_editor->setPlainText(QStringLiteral("Loading file asynchronously..."));
    syncMiniMapVisibility();

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
        syncMiniMapVisibility();
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
    syncMiniMapVisibility();
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
    m_multiCursors.clear();

    suspendHeavyEditorFeatures();
    if (m_gutter)
        m_gutter->setFoldingVisible(false);

    m_editor->setPlainText(QStringLiteral("Loading large file..."));
    syncMiniMapVisibility();
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
    auto sanitizeSelections = [this](const QList<QTextEdit::ExtraSelection>& in) {
        QList<QTextEdit::ExtraSelection> out;
        if (!m_editor || !m_editor->document() || in.isEmpty())
            return out;

        QTextDocument* doc = m_editor->document();
        const int maxPos = qMax(0, doc->characterCount() - 1);
        out.reserve(in.size());
        for (const QTextEdit::ExtraSelection& sel : in) {
            const QTextCursor cursor = sel.cursor;
            if (cursor.isNull() || cursor.document() != doc)
                continue;

            QTextEdit::ExtraSelection normalized = sel;
            QTextCursor safe(doc);
            const int anchor = qBound(0, cursor.anchor(), maxPos);
            const int pos = qBound(0, cursor.position(), maxPos);
            safe.setPosition(anchor);
            safe.setPosition(pos, QTextCursor::KeepAnchor);
            normalized.cursor = safe;
            out.append(normalized);
        }
        return out;
    };

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
    extras.append(multiCursorSelections());
    extras.append(m_bracketSelections);
    extras.append(m_searchSelections);
    m_editor->setExtraSelections(sanitizeSelections(extras));
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
    if (event->key() == Qt::Key_Escape && !m_multiCursors.isEmpty()) {
        clearExtraCursors();
        return true;
    }

    if ((event->modifiers() & Qt::ControlModifier)
        && (event->modifiers() & Qt::AltModifier)
        && event->key() == Qt::Key_Up) {
        return moveExtraCursorsVertically(-1);
    }
    if ((event->modifiers() & Qt::ControlModifier)
        && (event->modifiers() & Qt::AltModifier)
        && event->key() == Qt::Key_Down) {
        return moveExtraCursorsVertically(1);
    }

    if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_T) {
        static int themeIndex = 0;
        static const std::function<QEditorTheme()> themes[] = {
            QEditorTheme::oneDarkTheme, QEditorTheme::draculaTheme,
            QEditorTheme::monokaiTheme, QEditorTheme::solarizedDarkTheme,
            QEditorTheme::githubLightTheme, QEditorTheme::cursorDarkTheme,
            QEditorTheme::cursorDarkTheme
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
    if (m_liveIndentController && m_liveIndentController->handleKeyPress(event)) {
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
    updateActiveBracketGuide(true);
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
    if (!m_multiCursors.isEmpty()) {
        normalizeExtraCursors();
        updateCurrentLineHighlight();
    }

    // Search highlights from API-driven find operations are static snapshots.
    // Drop them after edits when the interactive search bar is not visible.
    if (!m_searchSelections.isEmpty() && (!m_searchBar || !m_searchBar->isVisible())) {
        m_searchSelections.clear();
        updateCurrentLineHighlight();
    }

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
        d_ptr->detachPlugins();
        d_ptr->cancelAsyncFileLoad();
        d_ptr->exitLargeFileMode();
        delete d_ptr->m_largeFileState;
        d_ptr->m_largeFileState = nullptr;
    }
}

void CodeEditor::setText(const QString& text) {
    d_ptr->cancelAsyncFileLoad();
    d_ptr->exitLargeFileMode();
    d_ptr->m_loadedFilePath.clear();
    d_ptr->m_loadedFileSize = 0;
    d_ptr->clearExtraCursors();
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
        d_ptr->syncMiniMapVisibility();
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
    d_ptr->m_loadedFilePath.clear();
    d_ptr->m_loadedFileSize = 0;
    d_ptr->clearExtraCursors();
    if (d_ptr->m_heavyFeaturesSuspended)
        d_ptr->resumeHeavyEditorFeatures();
    d_ptr->m_editor->clear();
    d_ptr->applyDocumentPerformanceMode(0);
    d_ptr->syncMiniMapVisibility();
}

bool CodeEditor::loadFile(const QString& filePath) {
    d_ptr->cancelAsyncFileLoad();
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const qint64 fileSize = f.size();
    f.close();
    d_ptr->m_loadedFilePath = filePath;
    d_ptr->m_loadedFileSize = fileSize;
    d_ptr->clearExtraCursors();

    if (d_ptr->shouldUseLargeFileMode(fileSize) && !d_ptr->m_preferEditableLargeFiles) {
        if (!d_ptr->enterLargeFileMode(filePath))
            return false;
        emit fileLoaded(filePath);
    } else if (d_ptr->shouldUseAsyncFullLoad(fileSize)
               || (d_ptr->shouldUseLargeFileMode(fileSize) && d_ptr->m_preferEditableLargeFiles)) {
        return d_ptr->startAsyncFileLoad(filePath);
    } else {
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        if (d_ptr->m_heavyFeaturesSuspended)
            d_ptr->resumeHeavyEditorFeatures();
        setText(QString::fromUtf8(f.readAll()));
        d_ptr->m_loadedFilePath = filePath;
        d_ptr->m_loadedFileSize = fileSize;
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
    if (d->m_zoomPointSize > 0)
        d->m_theme.fontSize = d->m_zoomPointSize;
    d->m_theme.fontSize = clampEditorPointSize(d->m_theme.fontSize);
    QFont editorFont(d->m_theme.fontFamily, d->m_theme.fontSize);
    editorFont.setPointSize(d->m_theme.fontSize);
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
    d->m_gutter->setTheme(d->m_theme);
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
    if (d->m_completer)      d->m_completer->setPopupTheme(d->m_theme);
    if (d->m_functionPopup)  d->m_functionPopup->setTheme(d->m_theme);
    if (d->m_searchBar)      d->m_searchBar->setTheme(d->m_theme);
    if (d->m_miniMap)        d->m_miniMap->setTheme(d->m_theme);
}

void CodeEditor::setThemeFromFile(const QString& jsonPath) { setTheme(QEditorTheme::fromJsonFile(jsonPath)); }
QEditorTheme CodeEditor::theme() const { return d_ptr->m_theme; }
void CodeEditor::setEditorFont(const QFont& font) {
    d_ptr->m_editor->setFont(font);
    d_ptr->m_zoomPointSize = clampEditorPointSize(editorPointSize(font));
    d_ptr->m_theme.fontFamily = font.family();
    d_ptr->m_theme.fontSize = d_ptr->m_zoomPointSize;
    applyEditorStyle(d_ptr->m_editor);
    d_ptr->updateLineNumberAreaWidth(0);
    d_ptr->updateCurrentLineHighlight();
}
QFont CodeEditor::editorFont() const { return d_ptr->m_editor->font(); }

void CodeEditor::setLineNumbersVisible(bool visible) {
    d_ptr->m_gutter->setLineNumbersVisible(visible);
    d_ptr->updateLineNumberAreaWidth(0);
}

void CodeEditor::setMiniMapVisible(bool visible)
{
    d_ptr->m_miniMapVisibleRequested = visible;
    d_ptr->syncMiniMapVisibility();
}

void CodeEditor::setEditableLargeFileMode(bool enabled)
{
    d_ptr->m_preferEditableLargeFiles = enabled;
    if (!enabled)
        return;

    if (d_ptr->m_largeFileMode
        && d_ptr->m_largeFileState
        && d_ptr->m_largeFileState->file.isOpen()) {
        const QString path = d_ptr->m_largeFileState->file.fileName();
        if (!path.isEmpty())
            d_ptr->startAsyncFileLoad(path);
    }
}

bool CodeEditor::editableLargeFileMode() const
{
    return d_ptr->m_preferEditableLargeFiles;
}

bool CodeEditor::isLargeFileWindowedMode() const
{
    return d_ptr->m_largeFileMode;
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

void CodeEditor::setAutoIndentEnabled(bool e)
{
    d_ptr->m_autoIndent = e;
    if (d_ptr->m_liveIndentController)
        d_ptr->m_liveIndentController->setEnabled(e);
}

void CodeEditor::setAutoBracketEnabled(bool e)
{
    d_ptr->m_autoBracket = e;
    if (d_ptr->m_liveIndentController)
        d_ptr->m_liveIndentController->setAutoBracketEnabled(e);
}
void CodeEditor::setBracketPairGuidesEnabled(bool enabled)
{
    if (d_ptr->m_bracketPairGuidesEnabled == enabled)
        return;

    d_ptr->m_bracketPairGuidesEnabled = enabled;
    if (!enabled)
        d_ptr->m_activeBracketGuide.clear();
    if (enabled && !d_ptr->m_largeDocumentMode && !d_ptr->m_heavyFeaturesSuspended && d_ptr->m_highlighter)
        d_ptr->m_highlighter->rehighlight();
    d_ptr->updateActiveBracketGuide(true);
}

bool CodeEditor::bracketPairGuidesEnabled() const
{
    return d_ptr->m_bracketPairGuidesEnabled;
}

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
    if (d_ptr->m_liveIndentController)
        d_ptr->m_liveIndentController->setTabWidth(spaces);
    d_ptr->m_editor->setTabStopDistance(
        QFontMetricsF(d_ptr->m_editor->font()).horizontalAdvance(' ') * spaces);
}

void CodeEditor::setInsertSpacesOnTab(bool spaces)
{
    d_ptr->m_insertSpaces = spaces;
    if (d_ptr->m_liveIndentController)
        d_ptr->m_liveIndentController->setInsertSpaces(spaces);
}

void CodeEditor::setIndentStylePreset(IndentStylePreset preset)
{
    d_ptr->m_indentStylePreset = preset;
    if (d_ptr->m_liveIndentController)
        d_ptr->m_liveIndentController->setStylePreset(preset);
}

IndentStylePreset CodeEditor::indentStylePreset() const
{
    return d_ptr->m_indentStylePreset;
}

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

    QTextDocument* doc = d_ptr->m_editor->document();
    if (!doc || term.isEmpty()) {
        findNext(term, caseSensitive, regex);
        return;
    }

    QTextDocument::FindFlags flags;
    if (caseSensitive)
        flags |= QTextDocument::FindCaseSensitively;
    const QRegularExpression re = buildSearchRegex(term, caseSensitive);

    struct ReplaceSpan {
        int start = 0;
        int end = 0;
        QString replacement;
    };

    QVector<ReplaceSpan> spans;
    QTextCursor cur(doc);
    const int scanMaxPos = qMax(0, doc->characterCount() - 1);
    int guardPos = -1;
    while (true) {
        cur = regex ? doc->find(re, cur)
                    : doc->find(term, cur, flags);
        if (cur.isNull())
            break;

        const int matchStart = cur.selectionStart();
        const int matchEnd = cur.selectionEnd();
        if (matchEnd <= matchStart) {
            const int nextPos = qMin(matchStart + 1, scanMaxPos);
            if (nextPos <= guardPos)
                break;
            guardPos = nextPos;
            cur.setPosition(nextPos);
            continue;
        }

        spans.append({matchStart, matchEnd,
                      buildReplacementText(cur.selectedText(), term, replacement, caseSensitive, regex)});
        guardPos = matchEnd;
        cur.setPosition(matchEnd);
    }

    if (!spans.isEmpty()) {
        const QString source = doc->toPlainText();
        QString replacedText;
        replacedText.reserve(source.size());

        int sourcePos = 0;
        for (const ReplaceSpan& span : spans) {
            if (span.start < sourcePos || span.end < span.start || span.end > source.size())
                continue;
            replacedText += source.mid(sourcePos, span.start - sourcePos);
            replacedText += span.replacement;
            sourcePos = span.end;
        }
        replacedText += source.mid(sourcePos);

        QTextCursor writer(doc);
        writer.beginEditBlock();
        writer.select(QTextCursor::Document);
        writer.insertText(replacedText);
        writer.endEditBlock();
    }

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
bool CodeEditor::addCursorAt(int line, int column)
{
    if (d_ptr->m_largeFileMode || d_ptr->m_asyncLoadInProgress)
        return false;

    if (!d_ptr->m_editor || !d_ptr->m_editor->document())
        return false;

    const int blockNo = qMax(0, line - 1);
    QTextBlock block = d_ptr->m_editor->document()->findBlockByNumber(blockNo);
    if (!block.isValid())
        return false;

    const int safeColumn = qMax(0, column - 1);
    const int lineLen = qMax(0, block.length() - 1);
    const int pos = block.position() + qMin(safeColumn, lineLen);
    const bool changed = d_ptr->addExtraCursorAtPosition(pos, true);
    if (changed) {
        d_ptr->updateCurrentLineHighlight();
        d_ptr->m_editor->viewport()->update();
    }
    return changed;
}

void CodeEditor::clearAdditionalCursors()
{
    d_ptr->clearExtraCursors();
}

int CodeEditor::additionalCursorCount() const
{
    return d_ptr->m_multiCursors.size();
}

void CodeEditor::setCustomKeywords(const QStringList& kw) { if (d_ptr->m_completer) d_ptr->m_completer->setCustomKeywords(kw); }
void CodeEditor::addCustomKeyword (const QString& kw)     { if (d_ptr->m_completer) d_ptr->m_completer->addCustomKeyword(kw); }
void CodeEditor::setReadOnly(bool r) {
    d_ptr->m_savedReadOnly = r;
    if (!d_ptr->m_largeFileMode)
        d_ptr->m_editor->setReadOnly(r);
}
bool CodeEditor::isReadOnly() const  { return d_ptr->m_editor->isReadOnly(); }

bool CodeEditor::registerPlugin(CodeEditorPlugin* plugin)
{
    if (!plugin)
        return false;

    const QString id = plugin->id().trimmed();
    if (id.isEmpty() || d_ptr->m_plugins.contains(id))
        return false;

    d_ptr->m_plugins.insert(id, plugin);
    plugin->onAttach(this);
    return true;
}

bool CodeEditor::unregisterPlugin(const QString& pluginId)
{
    auto it = d_ptr->m_plugins.find(pluginId);
    if (it == d_ptr->m_plugins.end())
        return false;

    if (it.value())
        it.value()->onDetach(this);
    d_ptr->m_plugins.erase(it);
    return true;
}

QStringList CodeEditor::pluginIds() const
{
    return d_ptr->m_plugins.keys();
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    d_ptr->updateLineNumberAreaWidth(0);
    if (d_ptr->m_miniMap)
        d_ptr->m_miniMap->update();
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
