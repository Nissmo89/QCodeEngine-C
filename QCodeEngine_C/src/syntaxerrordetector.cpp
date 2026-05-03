#include "syntaxerrordetector.h"
// #include "CodeEditor/diagnosticmanager.h"
#include <QTextBlock>
#include <QString>

SyntaxErrorDetector::SyntaxErrorDetector(QObject* parent)
    : QObject(parent) {}

void SyntaxErrorDetector::setDocument(QTextDocument* doc)  { m_doc = doc; }
void SyntaxErrorDetector::setDiagnosticManager(DiagnosticManager* dm) { m_dm = dm; }

// ---------------------------------------------------------------------------
//  analyze
//  Borrows TSTree* exactly like FoldManager/LineHighlighter do.
//  Walks entire tree, collects ERROR + MISSING nodes.
// ---------------------------------------------------------------------------
void SyntaxErrorDetector::analyze(void* treePtr)
{
    if (!treePtr || !m_doc || !m_dm) return;

    TSTree* tree = static_cast<TSTree*>(treePtr);
    m_pending.clear();

    walkNode(ts_tree_root_node(tree));

    const bool wasClean = m_syntaxClean;
    m_syntaxClean = m_pending.isEmpty();

    // Push to DiagnosticManager — it handles clearing old squiggles
    m_dm->setDiagnostics(m_pending);

    if (m_syntaxClean != wasClean)
        emit syntaxStateChanged(m_syntaxClean);
}

// ---------------------------------------------------------------------------
//  walkNode — recursive DFS
//  Tree-sitter guarantees ERROR/MISSING nodes appear in the right position
//  in the tree, so a simple DFS is sufficient.
// ---------------------------------------------------------------------------
void SyntaxErrorDetector::walkNode(TSNode node)
{
    if (ts_node_is_null(node)) return;

    const bool isError   = ts_node_is_error(node);
    const bool isMissing = ts_node_is_missing(node);

    if (isError || isMissing) {
        collectError(node);
        // Don't recurse into ERROR subtrees — the parent node
        // already covers the full bad range. Avoids duplicate squiggles.
        return;
    }

    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i)
        walkNode(ts_node_child(node, i));
}

// ---------------------------------------------------------------------------
//  collectError
//  Maps a TSNode to a Diagnostic with the right line/col/length.
// ---------------------------------------------------------------------------
void SyntaxErrorDetector::collectError(TSNode node)
{
    const TSPoint startPt = ts_node_start_point(node);
    const TSPoint endPt   = ts_node_end_point(node);

    const int line   = static_cast<int>(startPt.row);
    const int col    = static_cast<int>(startPt.column) / 2; // UTF-16 → char

    // Length: for single-line spans use the real width.
    // For multi-line errors, underline to end of start line.
    int length = 0;
    if (startPt.row == endPt.row) {
        length = static_cast<int>(endPt.column - startPt.column) / 2;
    } else {
        // Underline from error start to end of that line
        QTextBlock block = m_doc->findBlockByNumber(line);
        if (block.isValid())
            length = block.length() - col - 1;
    }

    // MISSING nodes have zero width — underline at least 1 char
    // so the squiggle is visible (e.g. missing semicolon)
    if (length <= 0) length = 1;

    // Build the message
    QString msg;
    if (ts_node_is_missing(node)) {
        // Tree-sitter tells us exactly what token was expected
        msg = QString("Syntax: missing '%1'")
                  .arg(QString::fromUtf8(ts_node_type(node)));
        // Bump column back 1 so squiggle is on the token BEFORE the gap
        if (col > 0) {
            // underline the last char of the previous token
            Diagnostic d;
            d.line     = line;
            d.column   = qMax(0, col - 1);
            d.length   = 1;
            d.message  = msg;
            d.severity = Diagnostic::Error;
            m_pending.append(d);
            return;
        }
    } else {
        msg = QString("Syntax error: unexpected '%1'")
        .arg(QString::fromUtf8(ts_node_type(node)));
    }

    Diagnostic d;
    d.line     = line;
    d.column   = col;
    d.length   = length;
    d.message  = msg;
    d.severity = Diagnostic::Error;
    m_pending.append(d);
}
