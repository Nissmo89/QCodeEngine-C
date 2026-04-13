#include "CodeEditor/CodeEditor.h"
#include "CodeEditor_p.h"
#include <QHBoxLayout>
#include <QFile>
#include <QTextBlock>
#include <QPainter>
#include <QVector>
#include "TreeSitterQuery_C.h"

extern "C" const TSLanguage *tree_sitter_c(void);

// ── Bracket matching (caret highlights matching delimiter) ───────────────────

namespace {

struct CLexer {
    enum Phase { Normal, LineComment, BlockComment, String, Char } phase = Normal;
    bool esc = false;

    // Only "code" (non-string, non-comment) brackets participate in matching.
    // String/char literals may contain parens that must not pair with delimiters outside.
    bool codeForBrackets() const {
        return phase == Normal;
    }

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
            if (esc) {
                esc = false;
                return;
            }
            if (c == QLatin1Char('\\')) {
                esc = true;
                return;
            }
            if (c == QLatin1Char('"'))
                phase = Normal;
            return;
        case Char:
            if (esc) {
                esc = false;
                return;
            }
            if (c == QLatin1Char('\\')) {
                esc = true;
                return;
            }
            if (c == QLatin1Char('\''))
                phase = Normal;
            return;
        case Normal:
            if (c == QLatin1Char('/') && i + 1 < s.size()) {
                QChar n = s.at(i + 1);
                if (n == QLatin1Char('/')) {
                    phase = LineComment;
                    return;
                }
                if (n == QLatin1Char('*')) {
                    phase = BlockComment;
                    return;
                }
            }
            if (c == QLatin1Char('"')) {
                phase = String;
                esc = false;
                return;
            }
            if (c == QLatin1Char('\'')) {
                phase = Char;
                esc = false;
                return;
            }
            return;
        }
    }
};

static void buildBracketCountableMask(const QString& s, QVector<bool>& mask) {
    const int n = s.size();
    mask.resize(n);
    CLexer lx;
    for (int i = 0; i < n; ++i) {
        mask[i] = lx.codeForBrackets();
        lx.push(s, i);
    }
}

static bool isBracketChar(QChar c) {
    return c == QLatin1Char('(') || c == QLatin1Char(')') || c == QLatin1Char('[') || c == QLatin1Char(']')
        || c == QLatin1Char('{') || c == QLatin1Char('}');
}

static bool isOpenBracket(QChar c) {
    return c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{');
}

static bool isCloseBracket(QChar c) {
    return c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}');
}

static QChar closingFor(QChar open) {
    if (open == QLatin1Char('('))
        return QLatin1Char(')');
    if (open == QLatin1Char('['))
        return QLatin1Char(']');
    if (open == QLatin1Char('{'))
        return QLatin1Char('}');
    return QChar();
}

// Returns partner index or -1 if unbalanced / mismatch.
static int findClosingPartner(const QString& s, const QVector<bool>& mask, int openPos) {
    QChar o = s.at(openPos);
    if (!isOpenBracket(o))
        return -1;
    QVector<QChar> stack;
    stack.push_back(closingFor(o));
    const int n = s.size();
    for (int i = openPos + 1; i < n; ++i) {
        if (!mask.at(i))
            continue;
        QChar c = s.at(i);
        if (isOpenBracket(c)) {
            stack.push_back(closingFor(c));
        } else if (isCloseBracket(c)) {
            if (stack.isEmpty())
                return -1;
            if (c != stack.last())
                return -1;
            stack.pop_back();
            if (stack.isEmpty())
                return i;
        }
    }
    return -1;
}

static int findOpeningPartner(const QString& s, const QVector<bool>& mask, int closePos) {
    QChar cl = s.at(closePos);
    if (!isCloseBracket(cl))
        return -1;
    QVector<QChar> stack;
    stack.push_back(cl);
    for (int i = closePos - 1; i >= 0; --i) {
        if (!mask.at(i))
            continue;
        QChar c = s.at(i);
        if (isOpenBracket(c)) {
            QChar wantClose = closingFor(c);
            if (stack.isEmpty())
                return -1;
            if (wantClose != stack.last())
                return -1;
            stack.pop_back();
            if (stack.isEmpty())
                return i;
        } else if (isCloseBracket(c)) {
            stack.push_back(c);
        }
    }
    return -1;
}

// Prefer bracket under caret: character after caret, else character before.
static int bracketIndexAtCursor(const QString& s, int cursorPos) {
    const int n = s.size();
    if (n == 0)
        return -1;
    // Guard stale or invalid positions (e.g. document shrunk after cursor was sampled).
    if (cursorPos >= 0 && cursorPos < n) {
        QChar c = s.at(cursorPos);
        if (isBracketChar(c))
            return cursorPos;
    }
    if (cursorPos > 0 && cursorPos - 1 < n) {
        QChar c = s.at(cursorPos - 1);
        if (isBracketChar(c))
            return cursorPos - 1;
    }
    return -1;
}

} // namespace

static FormatMap generateFormatMap(const QEditorTheme& theme) {
    FormatMap fmap;
    auto makeFormat = [](QColor color, bool bold = false, bool italic = false) {
        QTextCharFormat fmt;
        fmt.setForeground(color);
        if (bold) fmt.setFontWeight(QFont::Bold);
        if (italic) fmt.setFontItalic(true);
        return fmt;
    };

    // ── Keywords ──────────────────────────────────────────────────────────────
    // @keyword          — const, enum, extern, inline, sizeof, static, struct, typedef, union, volatile
    fmap["keyword"]             = makeFormat(theme.tokenKeyword, theme.keywordBold);
    // @keyword.control  — break, case, continue, default, do, else, for, goto, if, return, switch, while
    fmap["keyword.control"]     = makeFormat(theme.tokenKeywordControl, theme.keywordBold);
    // @keyword.preproc  — #define, #elif, #else, #endif, #if, #ifdef, #ifndef, #include, (preproc_directive)
    fmap["keyword.preproc"]     = makeFormat(theme.tokenKeywordPreproc, theme.keywordBold);
    // @preproc          — also used for the preproc group (alias in highlights.scm: @keyword.preproc @preproc)
    fmap["preproc"]             = makeFormat(theme.tokenKeywordPreproc);
    // @preproc.arg      — the raw token content after #define / #include
    fmap["preproc.arg"]         = makeFormat(theme.tokenPreprocessor);

    // ── Operators & Punctuation ───────────────────────────────────────────────
    // @operator         — all arithmetic, logical, comparison, assignment operators
    fmap["operator"]            = makeFormat(theme.tokenOperator);
    // @punctuation.delimiter — . ; ,
    fmap["punctuation.delimiter"] = makeFormat(theme.tokenPunctuation);
    // @punctuation.bracket   — { } ( ) [ ]
    fmap["punctuation.bracket"] = makeFormat(theme.tokenPunctuation);
    // generic parent fallback (resolver walks up dots)
    fmap["punctuation"]         = makeFormat(theme.tokenPunctuation);

    // ── Literals ─────────────────────────────────────────────────────────────
    // @string           — string_literal, system_lib_string, char_literal
    fmap["string"]              = makeFormat(theme.tokenString);
    // @string.escape    — \n, \t, \x41, … inside string/char literals
    fmap["string.escape"]       = makeFormat(theme.tokenEscape);
    // @number           — integer, float, hex, octal literals
    fmap["number"]              = makeFormat(theme.tokenNumber);
    // @boolean          — true / false
    fmap["boolean"]             = makeFormat(theme.tokenBoolean);
    // @constant.builtin — NULL, nullptr (node type "null")
    fmap["constant.builtin"]    = makeFormat(theme.tokenConstantBuiltin);
    // @constant         — ALL_CAPS identifiers matched by regex
    fmap["constant"]            = makeFormat(theme.tokenConstant);

    // ── Comments ─────────────────────────────────────────────────────────────
    // @comment          — // and /* */ comments
    fmap["comment"]             = makeFormat(theme.tokenComment, false, theme.commentItalic);

    // ── Identifiers ──────────────────────────────────────────────────────────
    // @variable         — plain (identifier) fallback
    fmap["variable"]            = makeFormat(theme.tokenIdentifier);

    // ── Functions ────────────────────────────────────────────────────────────
    // @function         — function declarators and call-expression targets
    fmap["function"]            = makeFormat(theme.tokenFunction, theme.functionBold);
    // @function.special — preproc_function_def macro names
    fmap["function.special"]    = makeFormat(theme.tokenKeywordPreproc, theme.functionBold);

    // ── Types ─────────────────────────────────────────────────────────────────
    // @type             — type_identifier, primitive_type, sized_type_specifier
    fmap["type"]                = makeFormat(theme.tokenType, theme.typeBold);

    // ── Struct / Record fields ────────────────────────────────────────────────
    // @property         — field_identifier (struct member access)
    fmap["property"]            = makeFormat(theme.tokenField);

    // ── Labels ────────────────────────────────────────────────────────────────
    // @label            — statement_identifier (goto labels)
    fmap["label"]               = makeFormat(theme.tokenLabel);

    // ── Attributes ───────────────────────────────────────────────────────────
    // @attribute        — GNU __attribute__((…)) and C23 [[attributes]]
    fmap["attribute"]           = makeFormat(theme.tokenAttribute);

    return fmap;
}

static void applyEditorStyle(QPlainTextEdit* editor, int lineHeightPx = 26) {
    QTextBlockFormat fmt;
    fmt.setLineHeight(lineHeightPx, QTextBlockFormat::FixedHeight);
    // Apply to the document default format so every new block inherits it,
    // and to the whole existing document.
    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.setBlockFormat(fmt);
    cursor.clearSelection();
    cursor.endEditBlock();
}

InnerEditor::InnerEditor(CodeEditorPrivate* d, QWidget* parent)
    : QPlainTextEdit(parent), d_ptr(d) {}

void InnerEditor::keyPressEvent(QKeyEvent* e) {
    if (d_ptr->m_completer && d_ptr->m_completer->handleKeyPress(e)) return;
    if (d_ptr->handleKeyPress(e)) return;
    QPlainTextEdit::keyPressEvent(e);
    
    if (d_ptr->m_completer && !e->text().isEmpty()) {
        d_ptr->m_completer->updatePopup();
    }
}

void InnerEditor::paintEvent(QPaintEvent* e) {
    QPlainTextEdit::paintEvent(e);

    QPainter painter(viewport());
    painter.setFont(font());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    qreal bottom = top + blockBoundingRect(block).height();

    while (block.isValid() && top <= e->rect().bottom()) {
        if (block.isVisible() && bottom >= e->rect().top()) {
            if (d_ptr->m_foldManager->isFolded(blockNumber)) {
                QString text = block.text();
                QFontMetrics fm(font());
                int textWidth = fm.horizontalAdvance(text.replace('\t', QString(d_ptr->m_tabWidth, ' ')));
                int blockH    = (int)(bottom - top); // actual row height (fixed at 26 px by style)

                painter.save();
                painter.setPen(d_ptr->m_theme.tokenComment);
                // Use the full block height so AlignVCenter lands in the middle of the row.
                painter.drawText(textWidth + 8, (int)top,
                                 fm.horizontalAdvance(" [...]"), blockH,
                                 Qt::AlignLeft | Qt::AlignVCenter, " [...]");
                painter.restore();
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + blockBoundingRect(block).height();
        ++blockNumber;
    }
}

CodeEditorPrivate::CodeEditorPrivate(CodeEditor* q, QWidget* parent)
    : QObject(parent), q_ptr(q), m_editor(new InnerEditor(this)) 
{
    m_gutter = new GutterWidget(m_editor, q);
    m_foldManager = new FoldManager(this);
    m_foldManager->setDocument(m_editor->document());

    QHBoxLayout* layout = new QHBoxLayout(q);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_gutter);
    layout->addWidget(m_editor);

    m_highlighter = new TreeSitterHighlighter(tree_sitter_c(), std::string(HIGHLIGHTS_SCM), generateFormatMap(m_theme), m_editor->document());
    connect(m_highlighter, &TreeSitterHighlighter::parsed, this, [this](void* treePtr) {
        m_foldManager->updateFoldRanges(treePtr, m_editor->document());

        QMap<int, int> foldRanges = m_foldManager->foldRanges();
        QList<FoldArea::FoldRange> ranges;
        for (auto it = foldRanges.begin(); it != foldRanges.end(); ++it) {
            ranges.append({it.key() + 1, it.value() + 1, m_foldManager->isFolded(it.key())});
        }
        m_gutter->setFoldRanges(ranges);
        m_gutter->update();
    });
    // After fold ranges rebuilt, refresh the gutter so fold arrows appear correctly
    connect(m_foldManager, &FoldManager::foldRangesUpdated, m_gutter, [this]() {
        m_gutter->update();
    });

    m_completer = new AutoCompleter(this);
    m_completer->setEditor(m_editor);

    // Disable word wrap by default for code
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);

    connect(m_editor, &QPlainTextEdit::blockCountChanged, this, &CodeEditorPrivate::updateLineNumberAreaWidth);
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditorPrivate::onCursorPositionChanged);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &CodeEditorPrivate::onTextChanged);

    connect(m_gutter, &GutterWidget::foldToggled, this, &CodeEditorPrivate::onGutterFoldClicked);

    // Hook contentsChange to re-apply line height after each edit (new blocks lose FixedHeight)
    connect(m_editor->document(), &QTextDocument::contentsChanged, m_editor, [this]() {
        // Only re-apply if blocks exist without our fixed height
        QTextBlock b = m_editor->document()->firstBlock();
        if (b.isValid() && b.blockFormat().lineHeightType() != QTextBlockFormat::FixedHeight) {
            applyEditorStyle(m_editor);
        }
    });

    applyEditorStyle(m_editor);
    updateLineNumberAreaWidth(0);
}

void CodeEditorPrivate::updateLineNumberAreaWidth(int /* newBlockCount */) {
    m_gutter->updateWidth();
}

void CodeEditorPrivate::updateLineNumberArea(const QRect& rect, int dy) {
    m_gutter->syncScrollWith(rect, dy);
    if (rect.contains(m_editor->viewport()->rect())) {
        updateLineNumberAreaWidth(0);
    }
}

void CodeEditorPrivate::updateGutterFoldRanges() {
    QMap<int, int> foldRanges = m_foldManager->foldRanges();
    QList<FoldArea::FoldRange> ranges;
    for (auto it = foldRanges.begin(); it != foldRanges.end(); ++it) {
        ranges.append({it.key() + 1, it.value() + 1, m_foldManager->isFolded(it.key())});
    }
    m_gutter->setFoldRanges(ranges);
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
    if (idx < 0)
        return;

    QVector<bool> mask;
    buildBracketCountableMask(text, mask);
    if (!mask.at(idx))
        return;

    auto makeSel = [doc, this](int from, int len, bool mismatch) {
        QTextEdit::ExtraSelection es;
        QTextCharFormat& f = es.format;
        if (mismatch) {
            f.setBackground(m_theme.bracketMismatchBackground);
            if (!m_theme.bracketMismatchBackground.isValid())
                f.setBackground(QColor(180, 60, 60, 90));
        } else {
            f.setBackground(m_theme.bracketMatchBackground);
            f.setForeground(m_theme.bracketMatchForeground);
        }
        es.cursor = QTextCursor(doc);
        es.cursor.setPosition(from);
        es.cursor.setPosition(from + len, QTextCursor::KeepAnchor);
        return es;
    };

    const QChar ch = text.at(idx);
    if (isOpenBracket(ch)) {
        const int partner = findClosingPartner(text, mask, idx);
        if (partner < 0) {
            m_bracketSelections.append(makeSel(idx, 1, true));
            return;
        }
        m_bracketSelections.append(makeSel(idx, 1, false));
        m_bracketSelections.append(makeSel(partner, 1, false));
        return;
    }
    if (isCloseBracket(ch)) {
        const int partner = findOpeningPartner(text, mask, idx);
        if (partner < 0) {
            m_bracketSelections.append(makeSel(idx, 1, true));
            return;
        }
        m_bracketSelections.append(makeSel(idx, 1, false));
        m_bracketSelections.append(makeSel(partner, 1, false));
    }
}

bool CodeEditorPrivate::handleKeyPress(QKeyEvent* event) {
    if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_T) {
        static int themeIndex = 0;
        static const std::function<QEditorTheme()> themes[] = {
            QEditorTheme::oneDarkTheme,
            QEditorTheme::draculaTheme,
            QEditorTheme::monokaiTheme,
            QEditorTheme::solarizedDarkTheme,
            QEditorTheme::githubLightTheme ,
            QEditorTheme::cursorDarkTheme
        };
        size_t themes_size = sizeof(themes) / sizeof(themes[0]);
        themeIndex = (themeIndex + 1) % themes_size;
        q_ptr->setTheme(themes[themeIndex]());
        return true;
    }


    if (event->key() == Qt::Key_Tab && m_editor->textCursor().hasSelection()) {
        indentSelection(true);
        return true;
    }
    if (event->key() == Qt::Key_Backtab) {
        indentSelection(false);
        return true;
    }
    if (event->key() == Qt::Key_Slash && (event->modifiers() & Qt::ControlModifier)) {
        toggleLineComment();
        return true;
    }
    
    if (m_autoIndent && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        QTextCursor cursor = m_editor->textCursor();
        QString currentLine = cursor.block().text();
        int spaces = 0;
        for (QChar ch : currentLine) {
            if (ch == ' ') spaces++;
            else if (ch == '\t') spaces += m_tabWidth;
            else break;
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
        static const QMap<QChar, QChar> pairs = {
            {'(', ')'}, {'[', ']'}, {'{', '}'}, {'"', '"'}, {'\'', '\''}
        };

        if (pairs.contains(typed)) {
            QTextCursor cursor = m_editor->textCursor();
            cursor.insertText(event->text() + pairs[typed]);
            cursor.movePosition(QTextCursor::Left);
            m_editor->setTextCursor(cursor);
            return true;
        }
    }
    
    return false;
}

void CodeEditorPrivate::indentSelection(bool indent) {
    QTextCursor cursor = m_editor->textCursor();
    int start = cursor.selectionStart();
    int end   = cursor.selectionEnd();
    
    cursor.beginEditBlock();
    QTextBlock block = m_editor->document()->findBlock(start);
    while (block.isValid() && block.position() <= end) {
        QTextCursor bc(block);
        if (indent) {
            bc.movePosition(QTextCursor::StartOfBlock);
            bc.insertText(m_insertSpaces ? QString(m_tabWidth, ' ') : "\t");
        } else {
            QString text = block.text();
            int toRemove = 0;
            for (int i = 0; i < qMin(m_tabWidth, text.size()); ++i) {
                if (text[i] == ' ') toRemove++;
                else if (text[i] == '\t') { toRemove = 1; break; }
                else break;
            }
            bc.movePosition(QTextCursor::StartOfBlock);
            bc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, toRemove);
            bc.removeSelectedText();
        }
        if (block.position() + block.length() - 1 >= end) break;
        block = block.next();
    }
    cursor.endEditBlock();
}

void CodeEditorPrivate::toggleLineComment() {
    QTextCursor cursor = m_editor->textCursor();
    int start = cursor.selectionStart();
    int end   = cursor.selectionEnd();
    
    cursor.beginEditBlock();
    QTextBlock block = m_editor->document()->findBlock(start);
    while (block.isValid() && block.position() <= end) {
        QTextCursor bc(block);
        bc.movePosition(QTextCursor::StartOfBlock);
        QString text = block.text().trimmed();
        if (text.startsWith("//")) {
            int pos = block.text().indexOf("//");
            bc.setPosition(block.position() + pos);
            bc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
            bc.removeSelectedText();
        } else {
            bc.insertText("//");
        }
        if (block.position() + block.length() - 1 >= end) break;
        block = block.next();
    }
    cursor.endEditBlock();
}

void CodeEditorPrivate::onCursorPositionChanged() {
    // ── Auto-unfold guard ──────────────────────────────────────────────────
    // If the cursor somehow lands on a hidden block (e.g. the user arrowed
    // into a folded region), find the fold that contains it and open it so
    // the cursor is never stranded inside invisible text.
    {
        QTextBlock curBlock = m_editor->textCursor().block();
        if (!curBlock.isVisible()) {
            int foldStart = m_foldManager->findFoldContaining(curBlock.blockNumber());
            if (foldStart >= 0) {
                m_foldManager->toggleFold(foldStart);
                m_gutter->update();
            }
        }
    }
    // ─────────────────────────────────────────────────────────────────────
    updateBracketMatch();
    updateCurrentLineHighlight();
    int blockNum = m_editor->textCursor().blockNumber();
    m_gutter->setCurrentLine(blockNum + 1);
    emit q_ptr->cursorPositionChanged(blockNum + 1, m_editor->textCursor().columnNumber() + 1);
}

void CodeEditorPrivate::onTextChanged() {
    emit q_ptr->textChanged();
}

void CodeEditorPrivate::onGutterFoldClicked(int line, bool /*folded*/) {
    m_foldManager->toggleFold(qMax(0, line - 1));
    updateGutterFoldRanges();
    m_gutter->update();
}


// =======================
// CodeEditor Public API
// =======================

CodeEditor::CodeEditor(QWidget* parent) : QWidget(parent), d_ptr(new CodeEditorPrivate(this, this)) {
    d_ptr->updateCurrentLineHighlight();
}

CodeEditor::~CodeEditor() = default;

void CodeEditor::setText(const QString& text) {
    d_ptr->m_editor->setPlainText(text);
    applyEditorStyle(d_ptr->m_editor);
}

QString CodeEditor::text() const {
    return d_ptr->m_editor->toPlainText();
}

void CodeEditor::insertText(const QString& text) {
    d_ptr->m_editor->textCursor().insertText(text);
}

void CodeEditor::clear() {
    d_ptr->m_editor->clear();
}

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

    // Build the editor font with Zed-like rendering quality
    QFont editorFont(theme.fontFamily, theme.fontSize);
    editorFont.setFixedPitch(true);
    editorFont.setStyleHint(QFont::Monospace);
    editorFont.setHintingPreference(QFont::PreferFullHinting);
    editorFont.setLetterSpacing(QFont::PercentageSpacing, 100);
    d->m_editor->setFont(editorFont);

    // Set document margin (like VSCode/Zed left gutter padding in text area)
    d->m_editor->document()->setDocumentMargin(6);

    QPalette pal = d->m_editor->palette();
    pal.setColor(QPalette::Base, theme.background);
    pal.setColor(QPalette::Text, theme.foreground);
    pal.setColor(QPalette::Highlight, theme.selectionBackground);
    pal.setColor(QPalette::HighlightedText, theme.selectionForeground);
    d->m_editor->setPalette(pal);

    d->m_gutter->setTheme(theme);
    if (d->m_highlighter) {
        d->m_highlighter->set_format_map(generateFormatMap(theme));
        d->m_highlighter->set_rainbow_colors(theme.rainbowColors);
        d->m_highlighter->rehighlight();
    }
    // Re-apply line height after font change (font metrics affect block layout)
    applyEditorStyle(d->m_editor);
    d->updateLineNumberAreaWidth(0);
    d->updateCurrentLineHighlight();
    if (d->m_completer)
        d->m_completer->setPopupTheme(theme);
}

void CodeEditor::setThemeFromFile(const QString& jsonPath) {
    setTheme(QEditorTheme::fromJsonFile(jsonPath));
}

QEditorTheme CodeEditor::theme() const {
    return d_ptr->m_theme;
}

void CodeEditor::setEditorFont(const QFont& font) {
    d_ptr->m_editor->setFont(font);
}

QFont CodeEditor::editorFont() const {
    return d_ptr->m_editor->font();
}

void CodeEditor::setLineNumbersVisible(bool visible) {
    d_ptr->m_gutter->setLineNumbersVisible(visible);
    d_ptr->updateLineNumberAreaWidth(0);
}

void CodeEditor::setMiniMapVisible(bool visible) {
    // Stub
}

void CodeEditor::setFoldingEnabled(bool enabled) {
    d_ptr->m_gutter->setFoldingVisible(enabled);
    d_ptr->updateLineNumberAreaWidth(0);
}

void CodeEditor::setAutoCompleteEnabled(bool enabled) {
    if (enabled) {
        if (!d_ptr->m_completer) {
            d_ptr->m_completer = new AutoCompleter(d_ptr.get());
            d_ptr->m_completer->setEditor(d_ptr->m_editor);
            d_ptr->m_completer->setPopupTheme(d_ptr->m_theme);
        }
    } else {
        if (d_ptr->m_completer) {
            d_ptr->m_completer->deleteLater();
            d_ptr->m_completer = nullptr;
        }
    }
}

void CodeEditor::setAutoIndentEnabled(bool enabled) {
    d_ptr->m_autoIndent = enabled;
}

void CodeEditor::setAutoBracketEnabled(bool enabled) {
    d_ptr->m_autoBracket = enabled;
}

void CodeEditor::setWordWrap(bool enabled) {
    d_ptr->m_editor->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

void CodeEditor::setShowWhitespace(bool visible) {
    QTextOption opt = d_ptr->m_editor->document()->defaultTextOption();
    if (visible)
        opt.setFlags(QTextOption::ShowTabsAndSpaces | QTextOption::ShowLineAndParagraphSeparators);
    else
        opt.setFlags(QTextOption::Flags());
    d_ptr->m_editor->document()->setDefaultTextOption(opt);
}

void CodeEditor::setTabWidth(int spaces) {
    d_ptr->m_tabWidth = spaces;
    d_ptr->m_editor->setTabStopDistance(QFontMetricsF(d_ptr->m_editor->font()).horizontalAdvance(' ') * spaces);
}

void CodeEditor::setInsertSpacesOnTab(bool spaces) {
    d_ptr->m_insertSpaces = spaces;
}

void CodeEditor::addGutterIcon(int line, GutterIconType type, const QString& tooltip) {
    int blockNum = qMax(0, line - 1);
    GutterIconInfo info{type, tooltip, nullptr};
    d_ptr->m_icons[blockNum] = info;
    d_ptr->m_gutter->setIconMap(d_ptr->m_icons);
}

void CodeEditor::removeGutterIcon(int line) {
    d_ptr->m_icons.remove(qMax(0, line - 1));
    d_ptr->m_gutter->setIconMap(d_ptr->m_icons);
}

void CodeEditor::clearGutterIcons() {
    d_ptr->m_icons.clear();
    d_ptr->m_gutter->setIconMap(d_ptr->m_icons);
}

void CodeEditor::foldLine(int line) {
    if (!d_ptr->m_foldManager->isFolded(line - 1)) {
        d_ptr->m_foldManager->toggleFold(line - 1);
        d_ptr->updateGutterFoldRanges();
    }
}
void CodeEditor::unfoldLine(int line) {
    if (d_ptr->m_foldManager->isFolded(line - 1)) {
        d_ptr->m_foldManager->toggleFold(line - 1);
        d_ptr->updateGutterFoldRanges();
    }
}
void CodeEditor::foldAll() {
    d_ptr->m_foldManager->foldAll();
    d_ptr->updateGutterFoldRanges();
}
void CodeEditor::unfoldAll() {
    d_ptr->m_foldManager->unfoldAll();
    d_ptr->updateGutterFoldRanges();
}

void CodeEditor::showSearchBar() { }
void CodeEditor::hideSearchBar() { }
int CodeEditor::findNext(const QString& term, bool caseSensitive, bool regex) { return 0; }
int CodeEditor::findPrev(const QString& term, bool caseSensitive, bool regex) { return 0; }
void CodeEditor::replaceNext(const QString& term, const QString& replacement) { }
void CodeEditor::replaceAll(const QString& term, const QString& replacement) { }

void CodeEditor::goToLine(int line) {
    QTextDocument* doc = d_ptr->m_editor->document();
    QTextBlock block = doc->findBlockByNumber(qMax(0, line - 1));
    if (block.isValid()) {
        QTextCursor cursor(block);
        d_ptr->m_editor->setTextCursor(cursor);
        d_ptr->m_editor->centerCursor();
    }
}

int CodeEditor::currentLine() const {
    return d_ptr->m_editor->textCursor().blockNumber() + 1;
}

int CodeEditor::currentColumn() const {
    return d_ptr->m_editor->textCursor().columnNumber() + 1;
}

QString CodeEditor::selectedText() const {
    return d_ptr->m_editor->textCursor().selectedText();
}

void CodeEditor::selectAll() {
    d_ptr->m_editor->selectAll();
}

void CodeEditor::setCustomKeywords(const QStringList& keywords) {
    if (d_ptr->m_completer)
        d_ptr->m_completer->setCustomKeywords(keywords);
}

void CodeEditor::addCustomKeyword(const QString& keyword) {
    if (d_ptr->m_completer)
        d_ptr->m_completer->addCustomKeyword(keyword);
}
void CodeEditor::setReadOnly(bool readOnly) { d_ptr->m_editor->setReadOnly(readOnly); }
bool CodeEditor::isReadOnly() const { return d_ptr->m_editor->isReadOnly(); }

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    d_ptr->updateLineNumberAreaWidth(0);
}
