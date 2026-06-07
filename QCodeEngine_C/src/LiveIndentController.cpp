#include "LiveIndentController.h"

#include <cstring>

#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>

#include "TreeSitterHighlighter.h"
#include "TreeSitterQuery_C.h"

extern "C" const TSLanguage *tree_sitter_c(void);

namespace {

constexpr char kIndentCaptureName[] = "indent";
constexpr char kEndCaptureName[] = "end";

int firstNonWhitespaceColumn(const QString& text)
{
    for (int i = 0; i < text.size(); ++i) {
        if (!text.at(i).isSpace())
            return i;
    }
    return -1;
}

TSNode childByFieldName(TSNode node, const char* name)
{
    return ts_node_child_by_field_name(node, name, static_cast<uint32_t>(std::strlen(name)));
}

bool isCompoundStatementNode(TSNode node)
{
    return !ts_node_is_null(node) && std::strcmp(ts_node_type(node), "compound_statement") == 0;
}

} // namespace

LiveIndentController::LiveIndentController(QPlainTextEdit* editor, TreeSitterHighlighter* highlighter)
    : m_editor(editor), m_highlighter(highlighter)
{
    uint32_t errorOffset = 0;
    TSQueryError errorType = TSQueryErrorNone;
    m_query = ts_query_new(tree_sitter_c(),
                           INDENTS_SCM,
                           static_cast<uint32_t>(std::strlen(INDENTS_SCM)),
                           &errorOffset,
                           &errorType);
}

LiveIndentController::~LiveIndentController()
{
    if (m_query)
        ts_query_delete(m_query);
}

void LiveIndentController::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void LiveIndentController::setTabWidth(int tabWidth)
{
    m_tabWidth = tabWidth;
}

void LiveIndentController::setInsertSpaces(bool insertSpaces)
{
    m_insertSpaces = insertSpaces;
}

void LiveIndentController::setAutoBracketEnabled(bool enabled)
{
    m_autoBracketEnabled = enabled;
}

void LiveIndentController::setStylePreset(IndentStylePreset preset)
{
    m_stylePreset = preset;
}

bool LiveIndentController::handleKeyPress(QKeyEvent* event)
{
    if (!m_editor)
        return false;

    QTextCursor cursor = m_editor->textCursor();
    if (!handleKeyPress(event, cursor, true))
        return false;

    m_editor->setTextCursor(cursor);
    return true;
}

bool LiveIndentController::handleKeyPress(QKeyEvent* event, QTextCursor& cursor, bool ownEditBlock)
{
    if (!m_enabled || !m_editor || !event)
        return false;

    auto runHandler = [ownEditBlock, &cursor](auto&& fn) {
        if (ownEditBlock)
            cursor.beginEditBlock();
        const bool handled = fn();
        if (ownEditBlock)
            cursor.endEditBlock();
        return handled;
    };

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        return runHandler([&]() { return handleNewline(cursor); });
    if (event->key() == Qt::Key_Backspace)
        return runHandler([&]() { return handleBackspace(cursor); });

    const QChar typed = event->text().isEmpty() ? QChar() : event->text().at(0);
    if (typed == QLatin1Char('}'))
        return runHandler([&]() { return handleClosingBrace(cursor); });
    if (typed == QLatin1Char('{'))
        return runHandler([&]() { return handleAllmanOpenBrace(cursor); });

    return false;
}

int LiveIndentController::safeTabWidth() const
{
    return qMax(1, m_tabWidth);
}

int LiveIndentController::indentationColumnsForRow(int row) const
{
    int level = 0;
    if (!queryIndentationLevelForRow(row, &level))
        return -1;
    return qMax(0, level) * safeTabWidth();
}

int LiveIndentController::fallbackIndentationForRow(int row) const
{
    if (!m_editor || !m_editor->document())
        return 0;

    const QTextBlock block = m_editor->document()->findBlockByNumber(row);
    if (!block.isValid())
        return previousNonEmptyIndentation(row);

    const QString text = block.text();
    const int firstCodeColumn = firstNonWhitespaceColumn(text);
    if (firstCodeColumn >= 0 && text.at(firstCodeColumn) == QLatin1Char('}'))
        return qMax(0, leadingWhitespaceColumns(text) - safeTabWidth());

    return previousNonEmptyIndentation(row);
}

int LiveIndentController::indentationColumnsForNewLine(const QTextCursor& cursor) const
{
    if (!m_editor)
        return 0;

    const QString blockText = cursor.block().text();
    const QString beforeCursor = blockText.left(cursor.positionInBlock());
    const int currentIndentColumns = leadingWhitespaceColumns(blockText);
    const int targetRow = cursor.blockNumber() + 1;
    const int fallbackColumns = fallbackIndentationForRow(targetRow);

    int desiredColumns = currentIndentColumns;
    if (beforeCursor.trimmed().isEmpty())
        desiredColumns = fallbackColumns;

    if (lineEndsWithOpeningToken(beforeCursor) || shouldIncreaseIndentAfterLine(beforeCursor))
        desiredColumns = currentIndentColumns + safeTabWidth();

    return qMax(0, desiredColumns);
}

int LiveIndentController::indentationColumnsForClosingLine(const QTextCursor& cursor) const
{
    if (!m_editor)
        return 0;

    const int syntaxColumns = indentationColumnsForRow(cursor.blockNumber());
    if (syntaxColumns >= 0)
        return syntaxColumns;
    return fallbackIndentationForRow(cursor.blockNumber());
}

int LiveIndentController::allmanOpenBraceIndentation(const QTextCursor& cursor) const
{
    if (!m_editor)
        return 0;

    const int currentIndentColumns = leadingWhitespaceColumns(cursor.block().text());
    const int previousIndentColumns = previousNonEmptyIndentation(cursor.blockNumber());
    const int syntaxColumns = indentationColumnsForRow(cursor.blockNumber());
    const int fallbackColumns = fallbackIndentationForRow(cursor.blockNumber());

    int desiredColumns = previousIndentColumns;
    if (syntaxColumns > 0)
        desiredColumns = qMin(desiredColumns, syntaxColumns);
    else if (fallbackColumns > 0)
        desiredColumns = qMin(desiredColumns, fallbackColumns);
    if (desiredColumns > currentIndentColumns)
        desiredColumns = currentIndentColumns;
    return qMax(0, desiredColumns);
}

int LiveIndentController::previousLessIndentedColumns(int fromBlockNumber, int currentColumns) const
{
    if (!m_editor || !m_editor->document())
        return 0;

    QTextBlock block = m_editor->document()->findBlockByNumber(fromBlockNumber);
    if (!block.isValid())
        block = m_editor->document()->lastBlock();
    else
        block = block.previous();

    while (block.isValid()) {
        const QString text = block.text();
        if (!text.trimmed().isEmpty()) {
            const int blockColumns = leadingWhitespaceColumns(text);
            if (blockColumns < currentColumns)
                return blockColumns;
        }
        block = block.previous();
    }

    return 0;
}

bool LiveIndentController::handleNewline(QTextCursor& cursor)
{
    if (cursor.hasSelection())
        return false;

    const QString lineText = cursor.block().text();
    const QString afterCursor = lineText.mid(cursor.positionInBlock());

    const QString innerIndent = indentationString(indentationColumnsForNewLine(cursor));

    const QChar immediateCloser = afterCursor.isEmpty() ? QChar() : afterCursor.at(0);
    const bool shouldSplitCloser = immediateCloser == QLatin1Char('}')
                                   || immediateCloser == QLatin1Char(')')
                                   || immediateCloser == QLatin1Char(']');

    if (shouldSplitCloser) {
        const QString closingIndent = currentLineIndentText(cursor);
        cursor.insertText(QStringLiteral("\n"));
        cursor.insertText(innerIndent);
        const int innerCursorPos = cursor.position();
        cursor.insertText(QStringLiteral("\n"));
        cursor.insertText(closingIndent);
        cursor.setPosition(innerCursorPos);
    } else {
        cursor.insertText(QStringLiteral("\n"));
        cursor.insertText(innerIndent);
    }
    return true;
}

bool LiveIndentController::handleBackspace(QTextCursor& cursor)
{
    if (cursor.hasSelection() || cursor.positionInBlock() <= 0)
        return false;

    const QString blockText = cursor.block().text();
    const int indentLength = leadingWhitespaceLength(blockText);
    if (cursor.positionInBlock() != indentLength)
        return false;

    const QString beforeCursor = blockText.left(cursor.positionInBlock());
    if (!linePrefixIsWhitespaceOnly(beforeCursor))
        return false;

    const int currentColumns = leadingWhitespaceColumns(beforeCursor);
    if (currentColumns <= 0)
        return false;

    const int targetColumns = previousLessIndentedColumns(cursor.blockNumber(), currentColumns);
    applyIndentationToBlock(cursor.blockNumber(), targetColumns);

    const QTextBlock updatedBlock = m_editor->document()->findBlockByNumber(cursor.blockNumber());
    cursor.setPosition(updatedBlock.position() + indentationString(targetColumns).size());
    return true;
}

bool LiveIndentController::handleClosingBrace(QTextCursor& cursor)
{
    if (cursor.hasSelection())
        return false;

    const QString beforeCursor = cursor.block().text().left(cursor.positionInBlock());
    if (!linePrefixIsWhitespaceOnly(beforeCursor))
        return false;

    QString lineText = cursor.block().text();
    const int blockNumber = cursor.blockNumber();
    const int column = cursor.positionInBlock();
    const bool skipExistingCloser = column < lineText.size() && lineText.at(column) == QLatin1Char('}');

    if (!skipExistingCloser)
        cursor.insertText(QStringLiteral("}"));

    const int desiredColumns = indentationColumnsForClosingLine(cursor);
    applyIndentationToBlock(blockNumber, desiredColumns);

    const QTextBlock updatedBlock = m_editor->document()->findBlockByNumber(blockNumber);
    const QString updatedText = updatedBlock.text();
    const QString indentText = indentationString(desiredColumns);
    int cursorColumn = indentText.size();
    if (cursorColumn < updatedText.size() && updatedText.at(cursorColumn) == QLatin1Char('}'))
        ++cursorColumn;

    cursor.setPosition(updatedBlock.position() + cursorColumn);
    return true;
}

bool LiveIndentController::handleAllmanOpenBrace(QTextCursor& cursor)
{
    if (m_stylePreset != IndentStylePreset::Allman)
        return false;

    if (cursor.hasSelection())
        return false;

    const QString beforeCursor = cursor.block().text().left(cursor.positionInBlock());
    const QString afterCursor = cursor.block().text().mid(cursor.positionInBlock());
    if (!linePrefixIsWhitespaceOnly(beforeCursor) || !afterCursor.trimmed().isEmpty())
        return false;

    const int blockNumber = cursor.blockNumber();
    const int desiredColumns = allmanOpenBraceIndentation(cursor);

    applyIndentationToBlock(blockNumber, desiredColumns);

    const QTextBlock updatedBlock = m_editor->document()->findBlockByNumber(blockNumber);
    cursor.setPosition(updatedBlock.position() + indentationString(desiredColumns).size());
    if (m_autoBracketEnabled) {
        cursor.insertText(QStringLiteral("{}"));
        cursor.movePosition(QTextCursor::Left);
    } else {
        cursor.insertText(QStringLiteral("{"));
    }
    return true;
}

bool LiveIndentController::queryIndentationLevelForRow(int row, int* outLevel) const
{
    if (outLevel)
        *outLevel = 0;

    if (!m_highlighter || !m_query || row < 0)
        return false;

    const TSTree* tree = m_highlighter->syntaxTree();
    const QString source = m_highlighter->sourceText();
    if (!tree || source.isNull())
        return false;

    TSNode root = ts_tree_root_node(tree);
    if (ts_node_is_null(root))
        return false;

    TSQueryCursor* cursor = ts_query_cursor_new();
    if (!cursor)
        return false;

    ts_query_cursor_exec(cursor, m_query, root);

    TSQueryMatch match;
    int level = 0;
    while (ts_query_cursor_next_match(cursor, &match)) {
        if (!matchPredicatesPass(match, source))
            continue;

        TSNode indentNode = {};
        TSNode endNode = {};
        bool hasIndent = false;
        bool hasEnd = false;

        for (uint16_t i = 0; i < match.capture_count; ++i) {
            const TSQueryCapture& capture = match.captures[i];
            uint32_t nameLength = 0;
            const char* captureName = ts_query_capture_name_for_id(m_query, capture.index, &nameLength);
            if (!captureName || nameLength == 0)
                continue;

            if (nameLength == sizeof(kIndentCaptureName) - 1
                && std::strncmp(captureName, kIndentCaptureName, nameLength) == 0) {
                indentNode = capture.node;
                hasIndent = true;
            } else if (nameLength == sizeof(kEndCaptureName) - 1
                       && std::strncmp(captureName, kEndCaptureName, nameLength) == 0) {
                endNode = capture.node;
                hasEnd = true;
            }
        }

        if (!hasIndent || shouldIgnoreIndentNode(indentNode))
            continue;

        const int startRow = static_cast<int>(ts_node_start_point(indentNode).row);
        if (row <= startRow)
            continue;

        if (hasEnd) {
            const int endRow = static_cast<int>(ts_node_start_point(endNode).row);
            if (row < endRow)
                ++level;
        } else {
            const int endRow = static_cast<int>(ts_node_end_point(indentNode).row);
            if (row <= endRow)
                ++level;
        }
    }

    ts_query_cursor_delete(cursor);

    if (outLevel)
        *outLevel = level;
    return true;
}

bool LiveIndentController::matchPredicatesPass(const TSQueryMatch& match, const QString& source) const
{
    uint32_t stepCount = 0;
    const TSQueryPredicateStep* steps = ts_query_predicates_for_pattern(m_query, match.pattern_index, &stepCount);
    if (!steps || stepCount == 0)
        return true;
    return evaluatePredicate(steps, stepCount, match, source);
}

bool LiveIndentController::evaluatePredicate(const TSQueryPredicateStep* steps,
                                             uint32_t stepCount,
                                             const TSQueryMatch& match,
                                             const QString& source) const
{
    uint32_t predicateStart = 0;
    while (predicateStart < stepCount) {
        uint32_t predicateEnd = predicateStart;
        while (predicateEnd < stepCount && steps[predicateEnd].type != TSQueryPredicateStepTypeDone)
            ++predicateEnd;

        if (predicateEnd == predicateStart) {
            predicateStart = predicateEnd + 1;
            continue;
        }

        if (steps[predicateStart].type == TSQueryPredicateStepTypeString) {
            uint32_t nameLength = 0;
            const char* predicateName = ts_query_string_value_for_id(
                m_query, steps[predicateStart].value_id, &nameLength);

            const bool isMatchPredicate = predicateName
                                          && nameLength == sizeof("#match?") - 1
                                          && std::strncmp(predicateName, "#match?", nameLength) == 0;
            const bool isNotMatchPredicate = predicateName
                                             && nameLength == sizeof("#not-match?") - 1
                                             && std::strncmp(predicateName, "#not-match?", nameLength) == 0;

            if (isMatchPredicate || isNotMatchPredicate) {
                if (predicateEnd - predicateStart < 3
                    || steps[predicateStart + 1].type != TSQueryPredicateStepTypeCapture
                    || steps[predicateStart + 2].type != TSQueryPredicateStepTypeString) {
                    return false;
                }

                uint32_t patternLength = 0;
                const char* patternText = ts_query_string_value_for_id(
                    m_query, steps[predicateStart + 2].value_id, &patternLength);
                const QRegularExpression regex(
                    QString::fromUtf8(patternText, static_cast<int>(patternLength)));

                bool matched = false;
                for (uint16_t i = 0; i < match.capture_count; ++i) {
                    const TSQueryCapture& capture = match.captures[i];
                    if (capture.index != steps[predicateStart + 1].value_id)
                        continue;
                    matched = regex.match(captureText(capture, source)).hasMatch();
                    if (matched)
                        break;
                }

                if (isMatchPredicate && !matched)
                    return false;
                if (isNotMatchPredicate && matched)
                    return false;
            }
        }

        predicateStart = predicateEnd + 1;
    }

    return true;
}

QString LiveIndentController::captureText(const TSQueryCapture& capture, const QString& source) const
{
    if (ts_node_is_null(capture.node))
        return {};

    const int start = static_cast<int>(ts_node_start_byte(capture.node) / 2);
    const int end = static_cast<int>(ts_node_end_byte(capture.node) / 2);
    if (end <= start || start < 0 || end > source.size())
        return {};
    return source.mid(start, end - start);
}

bool LiveIndentController::shouldIgnoreIndentNode(TSNode node) const
{
    if (ts_node_is_null(node))
        return true;

    const char* nodeType = ts_node_type(node);
    if (!nodeType)
        return false;

    if (std::strcmp(nodeType, "if_statement") == 0) {
        return isCompoundStatementNode(childByFieldName(node, "consequence"));
    }
    if (std::strcmp(nodeType, "for_statement") == 0
        || std::strcmp(nodeType, "while_statement") == 0
        || std::strcmp(nodeType, "do_statement") == 0) {
        return isCompoundStatementNode(childByFieldName(node, "body"));
    }
    if (std::strcmp(nodeType, "else_clause") == 0) {
        return isCompoundStatementNode(ts_node_named_child(node, 0));
    }

    return false;
}

void LiveIndentController::applyIndentationToBlock(int blockNumber, int columns)
{
    if (!m_editor || !m_editor->document())
        return;

    const QTextBlock block = m_editor->document()->findBlockByNumber(blockNumber);
    if (!block.isValid())
        return;

    const QString text = block.text();
    const int leadingLength = leadingWhitespaceLength(text);
    const QString desiredIndent = indentationString(columns);
    if (text.left(leadingLength) == desiredIndent)
        return;

    QTextCursor lineCursor(block);
    lineCursor.movePosition(QTextCursor::StartOfBlock);
    lineCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, leadingLength);
    lineCursor.insertText(desiredIndent);
}

QString LiveIndentController::indentationString(int columns) const
{
    columns = qMax(0, columns);
    if (m_insertSpaces)
        return QString(columns, QLatin1Char(' '));

    const int tabWidth = safeTabWidth();
    const int tabs = columns / tabWidth;
    const int spaces = columns % tabWidth;
    return QString(tabs, QLatin1Char('\t')) + QString(spaces, QLatin1Char(' '));
}

int LiveIndentController::leadingWhitespaceColumns(const QString& text) const
{
    int columns = 0;
    const int tabWidth = safeTabWidth();
    for (QChar ch : text) {
        if (ch == QLatin1Char(' ')) {
            ++columns;
        } else if (ch == QLatin1Char('\t')) {
            columns += tabWidth - (columns % tabWidth);
        } else {
            break;
        }
    }
    return columns;
}

int LiveIndentController::leadingWhitespaceLength(const QString& text) const
{
    int length = 0;
    while (length < text.size()) {
        const QChar ch = text.at(length);
        if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t'))
            break;
        ++length;
    }
    return length;
}

QString LiveIndentController::currentLineIndentText(const QTextCursor& cursor) const
{
    if (!m_editor)
        return {};

    const QString text = cursor.block().text();
    return text.left(leadingWhitespaceLength(text));
}

bool LiveIndentController::linePrefixIsWhitespaceOnly(const QString& text) const
{
    for (QChar ch : text) {
        if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t'))
            return false;
    }
    return true;
}

bool LiveIndentController::lineEndsWithOpeningToken(const QString& text) const
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;

    const QChar last = trimmed.at(trimmed.size() - 1);
    return last == QLatin1Char('{')
           || last == QLatin1Char('(')
           || last == QLatin1Char('[');
}

bool LiveIndentController::shouldIncreaseIndentAfterLine(const QString& text) const
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;
    if (trimmed.endsWith(QLatin1Char(';')) || trimmed.endsWith(QLatin1Char('}')))
        return false;
    if (trimmed == QStringLiteral("else") || trimmed == QStringLiteral("do"))
        return true;

    static const QRegularExpression controlPattern(
        QStringLiteral(R"(^(?:else\s+)?(?:if|for|while|switch)\b.*\)\s*$)"));
    return controlPattern.match(trimmed).hasMatch();
}

int LiveIndentController::previousNonEmptyIndentation(int fromBlockNumber) const
{
    if (!m_editor || !m_editor->document())
        return 0;

    QTextBlock block = m_editor->document()->findBlockByNumber(fromBlockNumber);
    if (!block.isValid())
        block = m_editor->document()->lastBlock();
    else
        block = block.previous();

    while (block.isValid()) {
        if (!block.text().trimmed().isEmpty())
            return leadingWhitespaceColumns(block.text());
        block = block.previous();
    }

    return 0;
}
