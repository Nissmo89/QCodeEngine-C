#include "treesitterhelper.h"
#include "qregularexpression.h"
#include <cstring> // for strcmp
#include <iostream>
#include <qtextobject.h>

TreeSitterHelper::TreeSitterHelper(const QString &sourceCode)
{
    // 1. Initialize Parser
    m_parser = ts_parser_new();
    ts_parser_set_language(m_parser, tree_sitter_c());

    // 2. Convert QString to UTF-8 Byte Array
    // IMPORTANT: Store this as a member so it persists as long as the tree exists
    m_sourceBytes = sourceCode.toUtf8();

    // 3. Parse the source code
    m_tree = ts_parser_parse_string(
        m_parser,
        NULL,
        m_sourceBytes.constData(),
        m_sourceBytes.length()
        );

    // 4. GET ROOT NODE
    TSNode root = ts_tree_root_node(m_tree);

    // 5. COLLECT FUNCTION LINES HERE
    functionLines.clear();
    functions.clear();
    collectFunctionRanges(root);
}

TreeSitterHelper::~TreeSitterHelper()
{
    // Clean up C-style resources
    if (m_tree) ts_tree_delete(m_tree);
    if (m_parser) ts_parser_delete(m_parser);
}

QVector<TSNode> TreeSitterHelper::getNodesByType(const QString &nodeType)
{
    QVector<TSNode> results;
    TSNode root = ts_tree_root_node(m_tree);

    // Convert target type to const char* for comparison
    QByteArray typeBytes = nodeType.toUtf8();

    traverseAndCollect(root, typeBytes.constData(), results);
    return results;
}

void TreeSitterHelper::traverseAndCollect(TSNode node, const char* targetType, QVector<TSNode> &outList)
{
    // 1. Check if the current node matches the type
    const char* currentType = ts_node_type(node);

    if (strcmp(currentType, targetType) == 0) {
        outList.append(node);
    }

    // 2. Iterate over children recursively
    uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_child(node, i);
        traverseAndCollect(child, targetType, outList);
    }
}

QString TreeSitterHelper::getNodeText(TSNode node) const
{
    uint32_t startByte = ts_node_start_byte(node);
    uint32_t endByte = ts_node_end_byte(node);

    // Extract snippet from the stored source bytes
    QByteArray snippet = m_sourceBytes.mid(startByte, endByte - startByte);
    return QString::fromUtf8(snippet);
}

QString TreeSitterHelper::extractFunctionSignature(TSNode funcNode) const
{
    if (ts_node_is_null(funcNode))
        return "";

    TSNode bodyNode = ts_node_child_by_field_name(funcNode, "body", 4);
    if (ts_node_is_null(bodyNode))
        return getNodeText(funcNode);

    uint32_t startByte = ts_node_start_byte(funcNode);
    uint32_t bodyByte  = ts_node_start_byte(bodyNode);

    if (bodyByte <= startByte)
        return getNodeText(funcNode);

    QByteArray rawBytes = m_sourceBytes.mid(startByte, bodyByte - startByte);
    QString cleaned = QString::fromUtf8(rawBytes);

    // ------- REMOVE COMMENTS -------
    cleaned.replace(QRegularExpression("/\\*.*?\\*/", QRegularExpression::DotMatchesEverythingOption), " ");
    cleaned.replace(QRegularExpression("//[^\n]*"), "");

    // ------- NORMALIZATION -------
    cleaned.replace("\r\n", " ");
    cleaned.replace("\n", " ");
    cleaned.replace("\r", " ");

    cleaned.replace(QRegularExpression("\\s+"), " "); // collapse whitespace
    cleaned = cleaned.trimmed();

    cleaned.replace(" (", "(");
    cleaned.replace("( ", "(");
    cleaned.replace(" )", ")");
    cleaned.replace(") ", ")");

    return cleaned;
}

// ============================================================================
// ✅ NEW: QPlainTextEdit-compatible helper functions
// ============================================================================

int getCursorByteOffset(QPlainTextEdit* editor)
{
    if (!editor) return 0;

    QTextCursor cursor = editor->textCursor();
    int line = cursor.blockNumber();
    int column = cursor.positionInBlock();

    // Calculate byte offset manually
    int byteOffset = 0;

    for (int i = 0; i < line; i++) {
        QTextBlock block = editor->document()->findBlockByNumber(i);
        QString blockText = block.text();
        byteOffset += blockText.toUtf8().size();
        byteOffset += 1; // for newline character
    }

    QTextBlock currentBlock = editor->document()->findBlockByNumber(line);
    QString currentLine = currentBlock.text();
    byteOffset += currentLine.left(column).toUtf8().size();

    return byteOffset;
}

TSNode getNodeAtCursor(TSTree* tree, QPlainTextEdit* editor)
{
    int byteOffset = getCursorByteOffset(editor);

    TSNode root = ts_tree_root_node(tree);

    return ts_node_descendant_for_byte_range(
        root,
        byteOffset,
        byteOffset
        );
}

std::vector<QString> getBreadcrumb(TSTree* tree, QPlainTextEdit* editor, const QByteArray& sourceUtf8)
{
    TSNode node = getNodeAtCursor(tree, editor);

    std::vector<QString> chain;

    TSNode n = node;
    while (!ts_node_is_null(n))
    {
        QString type = QString::fromUtf8(ts_node_type(n));

        // Capture only named scopes
        if (type == "function_definition" ||
            type == "function_declarator" ||
            type == "class_specifier" ||
            type == "namespace_definition" ||
            type == "struct_specifier")
        {
            TSNode nameNode = ts_node_child_by_field_name(n, "name", 4);

            if (!ts_node_is_null(nameNode))
            {
                uint32_t start = ts_node_start_byte(nameNode);
                uint32_t end   = ts_node_end_byte(nameNode);

                QString name = QString::fromUtf8(sourceUtf8.mid(start, end - start));
                chain.push_back(name);
            }
        }

        n = ts_node_parent(n);
    }

    std::reverse(chain.begin(), chain.end());
    return chain;
}

// ============================================================================
// SAFE TRAVERSAL
// ============================================================================
void traverseAndCount(TSNode node, int &count)
{
    if (ts_node_is_null(node)) return;

    const char* type = ts_node_type(node);
    if (type && std::string(type) == "function_definition")
    {
        count++;
        TSPoint p = ts_node_start_point(node);
        TSPoint s = ts_node_end_point(node);

        qDebug().noquote()
            << QString("function %1: [%2,%3] - [%4,%5]")
                   .arg(count)
                   .arg(p.row + 1)
                   .arg(p.column)
                   .arg(s.row + 1)
                   .arg(s.column);
    }

    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++)
        traverseAndCount(ts_node_child(node, i), count);
}

// ============================================================================
// SAFE PARSER
// ============================================================================
int countFunctionDefinitions(const std::string &source)
{
    if (source.empty()) {
        std::cerr << "ERROR: Source is empty\n";
        return 0;
    }

    // --- Create parser ---
    TSParser *parser = ts_parser_new();
    if (!parser) {
        std::cerr << "ERROR: Cannot allocate TSParser\n";
        return 0;
    }

    // --- Load language safely ---
    auto lang = tree_sitter_c();
    if (!lang) {
        std::cerr << "ERROR: tree_sitter_c() returned NULL!\n";
        ts_parser_delete(parser);
        return 0;
    }

    if (!ts_parser_set_language(parser, lang)) {
        std::cerr << "ERROR: Failed to set parser language\n";
        ts_parser_delete(parser);
        return 0;
    }

    // --- Parse code ---
    TSTree* tree = ts_parser_parse_string(
        parser, nullptr, source.c_str(), source.size()
        );

    if (!tree) {
        std::cerr << "ERROR: Failed to create TSTree\n";
        ts_parser_delete(parser);
        return 0;
    }

    TSNode root = ts_tree_root_node(tree);
    if (ts_node_is_null(root)) {
        std::cerr << "ERROR: root node is NULL\n";
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return 0;
    }

    // --- Traverse ---
    int count = 0;
    traverseAndCount(root, count);

    // --- Cleanup ---
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return count;
}

void TreeSitterHelper::collectFunctionLines(TSNode node)
{
    if (ts_node_is_null(node)) return;

    const char* type = ts_node_type(node);
    if (type && strcmp(type, "function_definition") == 0)
    {
        TSPoint p = ts_node_start_point(node);
        functionLines.push_back(p.row);   // store 0-based line
    }

    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++)
        collectFunctionLines(ts_node_child(node, i));
}

void TreeSitterHelper::collectFunctionRanges(TSNode node)
{
    if (ts_node_is_null(node)) return;

    if (strcmp(ts_node_type(node), "function_definition") == 0)
    {
        TSPoint s = ts_node_start_point(node);
        TSPoint e = ts_node_end_point(node);

        FunctionRange r;
        r.startLine = s.row;
        r.startCol  = s.column;
        r.endLine   = e.row;
        r.endCol    = e.column;
        r.node      = node;
        r.signature = extractFunctionSignature(node);

        functions.push_back(r);
    }

    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; ++i)
        collectFunctionRanges(ts_node_child(node, i));
}

int TreeSitterHelper::functionIndexAt(int line, int col) const
{
    for (int i = 0; i < functions.size(); ++i)
    {
        const auto &f = functions[i];

        bool afterStart =
            (line > f.startLine) ||
            (line == f.startLine && col >= f.startCol);

        bool beforeEnd =
            (line < f.endLine) ||
            (line == f.endLine && col <= f.endCol);

        if (afterStart && beforeEnd)
            return i;
    }
    return -1;
}
