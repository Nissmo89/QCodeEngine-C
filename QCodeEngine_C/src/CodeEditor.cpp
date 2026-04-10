#include "CodeEditor/CodeEditor.h"
#include "CodeEditor_p.h"
#include <QHBoxLayout>
#include <QFile>
#include <QTextBlock>
#include <QPainter>
#include "TreeSitterQuery_C.h"

extern "C" const TSLanguage *tree_sitter_c(void);

static FormatMap generateFormatMap(const QEditorTheme& theme) {
    FormatMap fmap;
    auto makeFormat = [](QColor color, bool bold = false, bool italic = false) {
        QTextCharFormat fmt;
        fmt.setForeground(color);
        if (bold) fmt.setFontWeight(QFont::Bold);
        if (italic) fmt.setFontItalic(true);
        return fmt;
    };
    fmap["keyword"]         = makeFormat(theme.tokenKeyword, theme.keywordBold);
    fmap["variable"]        = makeFormat(theme.tokenIdentifier);
    fmap["constant"]        = makeFormat(theme.tokenNumber);
    fmap["number"]          = makeFormat(theme.tokenNumber);
    fmap["string"]          = makeFormat(theme.tokenString);
    fmap["comment"]         = makeFormat(theme.tokenComment, false, theme.commentItalic);
    fmap["function"]        = makeFormat(theme.tokenFunction, theme.functionBold);
    fmap["function.special"]= makeFormat(theme.tokenPreprocessor);
    fmap["type"]            = makeFormat(theme.tokenType, theme.typeBold);
    fmap["property"]        = makeFormat(theme.tokenField);
    fmap["label"]           = makeFormat(theme.tokenIdentifier);
    fmap["operator"]        = makeFormat(theme.tokenOperator);
    fmap["delimiter"]       = makeFormat(theme.tokenOperator);
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
                // For a more accurate position due to tab expansion, we can use cursor layout, but fm.horizontalAdvance works decently as a fallback
                int textWidth = fm.horizontalAdvance(text.replace('\t', QString(d_ptr->m_tabWidth, ' ')));
                
                painter.save();
                painter.setPen(d_ptr->m_theme.tokenComment); // Faded color
                painter.drawText(textWidth + 8, top, fm.horizontalAdvance(" [...]"), fm.height(), Qt::AlignLeft | Qt::AlignVCenter, " [...]");
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
    m_gutter = new GutterWidget();
    m_gutter->setEditor(m_editor);
    m_foldManager = new FoldManager(this);
    m_foldManager->setDocument(m_editor->document());
    m_gutter->setFoldManager(m_foldManager);

    QHBoxLayout* layout = new QHBoxLayout(q);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_gutter);
    layout->addWidget(m_editor);

    m_highlighter = new TreeSitterHighlighter(tree_sitter_c(), std::string(HIGHLIGHTS_SCM), generateFormatMap(m_theme), m_editor->document());
    connect(m_highlighter, &TreeSitterHighlighter::parsed, this, [this](void* treePtr) {
        m_foldManager->updateFoldRanges(treePtr, m_editor->document());
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
    connect(m_editor, &QPlainTextEdit::updateRequest, this, &CodeEditorPrivate::updateLineNumberArea);
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditorPrivate::onCursorPositionChanged);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &CodeEditorPrivate::onTextChanged);

    connect(m_gutter, &GutterWidget::foldClicked,     this, &CodeEditorPrivate::onGutterFoldClicked);
    connect(m_gutter, &GutterWidget::iconClicked,     this, &CodeEditorPrivate::onGutterIconClicked);

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
    m_gutter->setFixedWidth(m_gutter->requiredWidth());
}

void CodeEditorPrivate::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy) {
        m_gutter->scroll(0, dy);
    } else {
        m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    }

    if (rect.contains(m_editor->viewport()->rect())) {
        updateLineNumberAreaWidth(0);
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
    
    extras.append(m_bracketSelections);
    extras.append(m_searchSelections);

    m_editor->setExtraSelections(extras);
}

void CodeEditorPrivate::updateBracketMatch() {
    m_bracketSelections.clear();
    QTextCursor cursor = m_editor->textCursor();
    // simplistic right now
}

bool CodeEditorPrivate::handleKeyPress(QKeyEvent* event) {
    if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_T) {
        static int themeIndex = 0;
        static const std::function<QEditorTheme()> themes[] = {
            QEditorTheme::oneDarkTheme,
            QEditorTheme::draculaTheme,
            QEditorTheme::monokaiTheme,
            QEditorTheme::solarizedDarkTheme,
            QEditorTheme::githubLightTheme
        };
        themeIndex = (themeIndex + 1) % 5;
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
    updateBracketMatch();
    updateCurrentLineHighlight();
    int blockNum = m_editor->textCursor().blockNumber();
    m_gutter->setCurrentBlock(blockNum);
    emit q_ptr->cursorPositionChanged(blockNum + 1, m_editor->textCursor().columnNumber() + 1);
}

void CodeEditorPrivate::onTextChanged() {
    emit q_ptr->textChanged();
}

void CodeEditorPrivate::onGutterFoldClicked(int blockNumber) {
    m_foldManager->toggleFold(blockNumber);
    m_gutter->update();
}

void CodeEditorPrivate::onGutterIconClicked(int blockNumber) {
    if (m_icons.contains(blockNumber)) {
        emit q_ptr->gutterIconClicked(blockNumber + 1, m_icons[blockNumber].type);
        if (m_icons[blockNumber].clickHandler) {
            m_icons[blockNumber].clickHandler();
        }
    }
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
}

void CodeEditor::setAutoCompleteEnabled(bool enabled) {
    if (enabled) {
        if (!d_ptr->m_completer) {
            d_ptr->m_completer = new AutoCompleter(d_ptr.get());
            d_ptr->m_completer->setEditor(d_ptr->m_editor);
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
    if (!d_ptr->m_foldManager->isFolded(line - 1)) d_ptr->m_foldManager->toggleFold(line - 1);
}
void CodeEditor::unfoldLine(int line) {
    if (d_ptr->m_foldManager->isFolded(line - 1)) d_ptr->m_foldManager->toggleFold(line - 1);
}
void CodeEditor::foldAll() { d_ptr->m_foldManager->foldAll(); }
void CodeEditor::unfoldAll() { d_ptr->m_foldManager->unfoldAll(); }

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

void CodeEditor::setCustomKeywords(const QStringList& keywords) { }
void CodeEditor::addCustomKeyword(const QString& keyword) { }
void CodeEditor::setReadOnly(bool readOnly) { d_ptr->m_editor->setReadOnly(readOnly); }
bool CodeEditor::isReadOnly() const { return d_ptr->m_editor->isReadOnly(); }

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    d_ptr->updateLineNumberAreaWidth(0);
}
