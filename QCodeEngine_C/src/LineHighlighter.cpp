// ============================================================================
//  LineHighlighter.cpp  –  QCodeEngine-C
//
//  Notebook-style line highlighter.  See LineHighlighter.h for the full
//  feature description.
//
//  Implementation notes:
//
//  1.  Comment text extraction
//      Tree-sitter gives us the byte offsets of each (comment) node.  We map
//      those bytes back to a QTextBlock using QTextDocument::findBlock(), then
//      extract the block's plain text.  This avoids maintaining a separate
//      copy of the source bytes.
//
//  2.  Tag parsing
//      The regex  \{(\d+),\s*(#[0-9A-Fa-f]{3,8})\}  is compiled once as a
//      static QRegularExpression and re-used across all calls — zero
//      recompilation cost per parse cycle.
//
//  3.  Alpha blending note
//      ExtraSelection backgrounds are drawn over the syntax-highlighted text.
//      Using a semi-transparent color (alpha < 255) lets the syntax colors
//      show through — similar to a real highlighter pen.  If the user supplies
//      a fully opaque color the syntax colors are covered but text remains
//      readable because only the background is set, not the foreground.
//
//  4.  Tree-sitter encoding
//      The highlighter parses with TSInputEncodingUTF16LE (Qt's native QString
//      encoding).  Byte offsets in the TSTree are therefore UTF-16 byte counts.
//      We divide by 2 to get the QTextDocument character position.
// ============================================================================
#include "CodeEditor/LineHighlighter.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QRegularExpression>
#include <QDebug>
#include <algorithm>

extern "C" { const TSLanguage* tree_sitter_c(void); }
// ---------------------------------------------------------------------------
LineHighlighter::LineHighlighter(QObject* parent)
    : QObject(parent)
{}

void LineHighlighter::setDocument(QTextDocument* doc)
{
    m_doc = doc;
}

void LineHighlighter::setEditor(QPlainTextEdit* editor)
{
    m_editor = editor;
}

void LineHighlighter::clear()
{
    m_lineColors.clear();
    m_selections.clear();
    emit highlightChanged();
}

// ---------------------------------------------------------------------------
//  updateFromTree
//
//  Borrows the TSTree* from TreeSitterHighlighter.  Runs a comment query,
//  parses each comment for the {N,#COLOR} tag, then rebuilds the line-color
//  map and the ExtraSelection list.
// ---------------------------------------------------------------------------
void LineHighlighter::updateFromTree(void* treePtr, QTextDocument* doc)
{
    if (!treePtr) return;
    if (doc && doc != m_doc) m_doc = doc;
    if (!m_doc) return;

    TSTree* tree = static_cast<TSTree*>(treePtr);

    // ── Build a tiny TS query for comment nodes ──────────────────────────────
    // We create it lazily (once) via a function-local static.
    // The language pointer is embedded in the tree so we can retrieve it.
    static TSQuery* s_query    = nullptr;
    static bool     s_queryOk  = false;

    if (!s_query) {
        // extern "C" const TSLanguage* tree_sitter_c(void);
        const TSLanguage* lang = tree_sitter_c();
        uint32_t errOff = 0;
        TSQueryError errType = TSQueryErrorNone;
        const char* scheme = "(comment) @comment";
        s_query = ts_query_new(lang, scheme, (uint32_t)strlen(scheme),
                               &errOff, &errType);
        s_queryOk = (s_query != nullptr);
        if (!s_queryOk)
            qWarning() << "LineHighlighter: comment query failed at offset" << errOff;
    }

    m_lineColors.clear();

    if (s_queryOk) {
        TSQueryCursor* cursor = ts_query_cursor_new();
        ts_query_cursor_exec(cursor, s_query, ts_tree_root_node(tree));

        TSQueryMatch match;
        uint32_t     captureIndex = 0;

        while (ts_query_cursor_next_capture(cursor, &match, &captureIndex)) {
            const TSNode node = match.captures[captureIndex].node;
            if (ts_node_is_error(node) || ts_node_is_missing(node)) continue;

            // ── Map the node's byte offset to a QTextDocument position ───────
            // The document is parsed as UTF-16LE, so byte offset / 2 = char pos.
            const uint32_t startByte = ts_node_start_byte(node);
            const int      charPos   = static_cast<int>(startByte) / 2;
            const QTextBlock block   = m_doc->findBlock(charPos);
            if (!block.isValid()) continue;

            // ── Extract comment text and parse the tag ────────────────────────
            const QString text = block.text();
            int    count = 0;
            QColor color;
            if (!parseTag(text, count, color)) continue;

            // ── Populate line-color map for the N lines AFTER the comment ─────
            const int anchorRow = block.blockNumber();   // comment line itself
            const int lastRow   = anchorRow + count;     // inclusive

            for (int row = anchorRow + 1; row <= lastRow; ++row) {
                // Later tags win: if two tag ranges overlap, the last one
                // encountered in document order takes precedence.
                m_lineColors.insert(row, color);
            }
        }

        ts_query_cursor_delete(cursor);
    }

    rebuildSelections();
    emit highlightChanged();
}

// ---------------------------------------------------------------------------
//  parseTag
//
//  Matches  // {N,#COLOR}  or  /* {N,#COLOR} */  anywhere in `text`.
//  Accepted color formats: #RGB, #RRGGBB, #RRGGBBAA (3/6/8 hex digits).
//  Returns true and fills outCount/outColor on success.
// ---------------------------------------------------------------------------
bool LineHighlighter::parseTag(const QString& text,
                               int& outCount, QColor& outColor)
{
    // Compiled once per process — thread-safe after Qt 5.
    static const QRegularExpression s_re(
        QStringLiteral(R"(\{(\d+),\s*(#[0-9A-Fa-f]{3,8})\})"),
        QRegularExpression::CaseInsensitiveOption
    );

    const QRegularExpressionMatch m = s_re.match(text);
    if (!m.hasMatch()) return false;

    const int count = m.captured(1).toInt();
    if (count <= 0 || count > 10000) return false;   // sanity cap

    // Expand shorthand  #RGB  →  #RRGGBB  before feeding to QColor
    QString hex = m.captured(2);  // e.g. "#F00" or "#FF0000"
    if (hex.length() == 4) {
        // #RGB → #RRGGBB
        hex = QStringLiteral("#")
              + QString(2, hex[1])
              + QString(2, hex[2])
              + QString(2, hex[3]);
    }

    const QColor color(hex);
    if (!color.isValid()) return false;

    // If fully opaque, use a semi-transparent version so syntax colors
    // bleed through (notebook-pen effect).  The user can override this
    // by explicitly supplying an alpha value in the 8-digit form.
    if (color.alpha() == 255) {
        QColor blended = color;
        blended.setAlpha(80);   // ~31 % opacity — adjust to taste
        outColor = blended;
    } else {
        outColor = color;
    }

    outCount = count;
    return true;
}

// ---------------------------------------------------------------------------
//  rebuildSelections
//
//  Converts the m_lineColors map into a QList<QTextEdit::ExtraSelection>
//  with FullWidthSelection = true so each entry covers the full line width.
// ---------------------------------------------------------------------------
void LineHighlighter::rebuildSelections()
{
    m_selections.clear();
    if (!m_doc || m_lineColors.isEmpty()) return;

    m_selections.reserve(m_lineColors.size());

    for (auto it = m_lineColors.begin(); it != m_lineColors.end(); ++it) {
        const QTextBlock block = m_doc->findBlockByNumber(it.key());
        if (!block.isValid()) continue;

        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(it.value());
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = QTextCursor(block);
        sel.cursor.clearSelection();
        m_selections.append(sel);
    }
}

// ---------------------------------------------------------------------------
//  O(1) queries
// ---------------------------------------------------------------------------
QColor LineHighlighter::colorForLine(int blockNumber) const
{
    return m_lineColors.value(blockNumber, QColor());
}

bool LineHighlighter::isHighlighted(int blockNumber) const
{
    return m_lineColors.contains(blockNumber);
}
