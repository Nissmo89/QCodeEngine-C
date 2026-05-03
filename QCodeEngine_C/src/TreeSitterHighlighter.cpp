#include <QDebug>
#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#include <tree_sitter/api.h>

#include "TreeSitterHighlighter.h"

namespace {

QString documentRangeText(QTextDocument* document, int start, int end)
{
    if (!document)
        return {};

    const int maxPos = qMax(0, document->characterCount() - 1);
    start = qBound(0, start, maxPos);
    end = qBound(0, end, maxPos);
    if (end <= start)
        return {};

    QTextCursor cursor(document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return text;
}

} // namespace


// Minimal initisliser.
TreeSitterHighlighter::TreeSitterHighlighter(const TSLanguage* language, QTextDocument* document): QObject(document) {
    // Set the language
    this->language = language;

    // Create a parser and set its language
    this->parser = ts_parser_new();
    if (!this->parser) {
        qCritical() << "TreeSitterHighlighter: failed to create TSParser";
    } else if (!this->language || !ts_parser_set_language(this->parser, this->language)) {
        qWarning() << "TreeSitterHighlighter: parser language setup failed";
    }

    // Create an empty query
    uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;
    this->query = (this->language != nullptr)
                      ? ts_query_new(this->language, "", 0, &error_offset, &error_type)
                      : nullptr;

    // Create emty tree
    this->tree = this->parser
                     ? ts_parser_parse_string_encoding(this->parser, NULL, "", 0, TSInputEncodingUTF16LE)
                     : nullptr;

    // Create emtpty format map
    this->format_map = FormatMap();

    // Create empty lookup table
    this->format_lookup_table = new QTextCharFormat[0];

    // Set the document
    this->set_document(document);
}

// Full initialiser.
TreeSitterHighlighter::TreeSitterHighlighter(const TSLanguage* language,
                                             std::string query_string,
                                             FormatMap format_map,
                                             QTextDocument* document):
    TreeSitterHighlighter(language, document) {
    // Save the format map
    this->format_map = format_map;

    // Create query
    this->set_query(query_string);

    // Highlight the document
    this->rehighlight();
}

// Destructor.
TreeSitterHighlighter::~TreeSitterHighlighter() {
    ts_parser_delete(this->parser);
    ts_tree_delete(this->tree);
    ts_query_delete(this->query);
    delete[] this->format_lookup_table;
    this->document = nullptr;
}

// Set the highlight query.
// The return value indicates success, if not successful an empty query is created instead.
// bool TreeSitterHighlighter::set_query(std::string query_string) {
//     ts_query_delete(this->query);

//     uint32_t error_offset;
//     TSQueryError error_type;
//     this->query = ts_query_new(this->language, query_string.c_str(), query_string.length(), &error_offset, &error_type);
//     if (!this->query) {
//         qDebug() << "The highlight query could not be created due to error" << error_type << " at char " << error_offset << ".";
//         this->query = ts_query_new(this->language, "", 0, &error_offset, &error_type);
//         return false;
//     }

//     generate_format_table();
//     return true;
// }
//
// Pass by const reference (&) to avoid copying the large query string
bool TreeSitterHighlighter::set_query(const std::string& query_string) {
    // Note: ensure this->query is initialized to nullptr in your constructor,
    // otherwise this first deletion could cause a segfault with garbage memory!
    if (this->query) {
        ts_query_delete(this->query);
        this->query = nullptr;
    }

    uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;

    if (!this->language) {
        generate_format_table();
        return false;
    }

    this->query = ts_query_new(this->language, query_string.c_str(), query_string.length(), &error_offset, &error_type);
    const bool query_from_input = (this->query != nullptr);

    if (!this->query) {
        qDebug() << "The highlight query could not be created due to error" << error_type << " at char " << error_offset << ".";
        // Create an empty fallback query
        this->query = ts_query_new(this->language, "", 0, &error_offset, &error_type);
        if (!this->query) {
            generate_format_table();
            return false;
        }
    }

    generate_format_table();
    return query_from_input;
}


// Set the format map.
void TreeSitterHighlighter::set_format_map(FormatMap format_map) {
    this->format_map = format_map;
    generate_format_table();
}

// Return the format map.
const FormatMap TreeSitterHighlighter::get_format_map() {
    return this->format_map;
}

// Set rainbow colors.
void TreeSitterHighlighter::set_rainbow_colors(const QList<QColor>& colors) {
    this->m_rainbowColors = colors;
}

void TreeSitterHighlighter::setPerformanceMode(bool enabled)
{
    m_performanceMode = enabled;
}

// Set the QTextDocument on which the syntax hihglighter is installed.
void TreeSitterHighlighter::set_document(QTextDocument* document) {
    if (this->document) {
        disconnect(this->document, SIGNAL(contentsChange(int, int, int)), this, SLOT(source_changed(int, int, int)));
    }
    if (this->document && document && this->document != document)
        this->clear();
    this->document = document;
    if (this->document) {
        this->m_sourceCache = this->document->toPlainText();
        this->reparse();
        connect(this->document, SIGNAL(contentsChange(int, int, int)), this, SLOT(source_changed(int, int, int)));
    } else {
        this->m_sourceCache.clear();
    }
}

// Return the QTextDocument on which this syntax highlighter is installed.
const QTextDocument* TreeSitterHighlighter::get_document() {
    return this->document;
}

// Handle a changed source.
void TreeSitterHighlighter::source_changed(int position, int charsRemoved, int charsAdded) {
    if (charsRemoved > 0) {
        const int removePos = qBound(0, position, this->m_sourceCache.size());
        const int removeLen = qBound(0, charsRemoved, this->m_sourceCache.size() - removePos);
        if (removeLen > 0)
            this->m_sourceCache.remove(removePos, removeLen);
    }
    if (charsAdded > 0 && this->document) {
        const QString inserted = documentRangeText(this->document, position, position + charsAdded);
        const int insertPos = qBound(0, position, this->m_sourceCache.size());
        this->m_sourceCache.insert(insertPos, inserted);
    }

    // reparse
    BlockRange changed_range = this->reparse_range(position, position + charsRemoved, position + charsAdded);
    if (m_performanceMode && this->document) {
        QTextBlock block = this->document->findBlock(position);
        const int line = block.isValid() ? block.blockNumber() : 0;
        const int first = qMax(0, line - 80);
        const int last = qMin(qMax(0, this->document->blockCount() - 1), line + 300);
        changed_range = BlockRange(first, last);
    }

    // rehighlight
    this->rehighlight_range(changed_range);
    emit parsed(static_cast<void*>(this->tree));
}

// Reparse the selected block range.
BlockRange TreeSitterHighlighter::reparse_range(int start, int old_end, int new_end) {
    if (!this->document || !this->parser) {
        return BlockRange(0, 0);
    }

    if (!this->tree) {
        this->reparse();
        return BlockRange(this->document->firstBlock().blockNumber(),
                          this->document->lastBlock().blockNumber());
    }

    QTextBlock start_block = this->document->findBlock(start);
    QTextBlock old_end_block = this->document->findBlock(old_end);
    QTextBlock new_end_block = this->document->findBlock(new_end);
    if (!start_block.isValid())
        start_block = this->document->firstBlock();
    if (!old_end_block.isValid())
        old_end_block = this->document->lastBlock();
    if (!new_end_block.isValid())
        new_end_block = this->document->lastBlock();

    TSInputEdit edit {};
    edit.start_byte = static_cast<uint32_t>(qMax(0, start) * 2);
    edit.old_end_byte = static_cast<uint32_t>(qMax(0, old_end) * 2);
    edit.new_end_byte = static_cast<uint32_t>(qMax(0, new_end) * 2);
    edit.start_point = TSPoint{
        static_cast<uint32_t>(qMax(0, start_block.blockNumber())),
        static_cast<uint32_t>(qMax(0, start - start_block.position()) * 2)
    };
    edit.old_end_point = TSPoint{
        static_cast<uint32_t>(qMax(0, old_end_block.blockNumber())),
        static_cast<uint32_t>(qMax(0, old_end - old_end_block.position()) * 2)
    };
    edit.new_end_point = TSPoint{
        static_cast<uint32_t>(qMax(0, new_end_block.blockNumber())),
        static_cast<uint32_t>(qMax(0, new_end - new_end_block.position()) * 2)
    };

    TSTree* old_tree = this->tree;
    ts_tree_edit(old_tree, &edit);
    TSTree* new_tree = ts_parser_parse_string_encoding(
        this->parser,
        old_tree,
        reinterpret_cast<const char*>(this->m_sourceCache.utf16()),
        this->m_sourceCache.length() * 2,
        TSInputEncodingUTF16LE);

    if (!new_tree) {
        this->tree = old_tree;
        const int a = qMax(0, start_block.blockNumber());
        const int b = qMax(a, new_end_block.blockNumber());
        return BlockRange(a, b);
    }

    this->tree = new_tree;

    int changed_ranges_start_pos = qMax(0, start);
    int changed_ranges_end_pos = qMax(changed_ranges_start_pos, std::max(old_end, new_end));
    if (!m_performanceMode) {
        uint32_t changed_ranges_length = 0;
        TSRange* changed_ranges = ts_tree_get_changed_ranges(old_tree, this->tree, &changed_ranges_length);
        for (uint32_t i = 0; i != changed_ranges_length; i++) {
            const TSRange range = changed_ranges[i];
            changed_ranges_start_pos = std::min(changed_ranges_start_pos, static_cast<int>(range.start_byte / 2));
            changed_ranges_end_pos = std::max(changed_ranges_end_pos, static_cast<int>(range.end_byte / 2));
        }
        free(changed_ranges);
    }

    ts_tree_delete(old_tree);

    QTextBlock changedStartBlock = this->document->findBlock(changed_ranges_start_pos);
    QTextBlock changedEndBlock = this->document->findBlock(changed_ranges_end_pos);
    if (!changedStartBlock.isValid())
        changedStartBlock = this->document->firstBlock();
    if (!changedEndBlock.isValid())
        changedEndBlock = this->document->lastBlock();
    return BlockRange(changedStartBlock.blockNumber(), changedEndBlock.blockNumber());
}

// Reparse the whole document.
void TreeSitterHighlighter::reparse() {
    if (!this->document || !this->parser) {
        return;
    }

    ts_tree_delete(this->tree);
    this->m_sourceCache = this->document->toPlainText();
    this->tree = ts_parser_parse_string_encoding(this->parser, NULL, (char*)this->m_sourceCache.utf16(), this->m_sourceCache.length() * 2, TSInputEncodingUTF16LE);
}

// Clear the given block range.
inline void TreeSitterHighlighter::clear_range(BlockRange range) {
    if (!this->document) {
        return;
    }

    QTextBlock block = this->document->findBlockByNumber(range.first);
    while (block.isValid() && block.blockNumber() <= range.second) {
        if (block.layout())
            block.layout()->clearFormats();
        block = block.next();
    }
}

// Clear the whole document.
inline void TreeSitterHighlighter::clear() {
    if (!this->document) {
        return;
    }

    BlockRange range = BlockRange(this->document->firstBlock().blockNumber(), this->document->lastBlock().blockNumber());
    this->clear_range(range);
    this->document->markContentsDirty(0, this->document->characterCount());
}

// Rehighlight the changed range.
void TreeSitterHighlighter::rehighlight_range(BlockRange changed_range) {
    if (!this->document || !this->query || !this->tree) {
        return;
    }

    // evaluate query in changed range
    QTextBlock start_block = this->document->findBlockByNumber(changed_range.first);
    QTextBlock stop_block = this->document->findBlockByNumber(changed_range.second);
    if (!start_block.isValid())
        start_block = this->document->firstBlock();
    if (!stop_block.isValid())
        stop_block = this->document->lastBlock();
    if (!start_block.isValid() || !stop_block.isValid())
        return;

    const uint32_t capture_count = ts_query_capture_count(this->query);
    TSQueryCursor* query_cursor = ts_query_cursor_new();
    ts_query_cursor_set_byte_range(query_cursor, start_block.position() * 2, (stop_block.position() + stop_block.length()) * 2);
    ts_query_cursor_exec(query_cursor, this->query, ts_tree_root_node(this->tree));

    // Clear changed blocks
    this->clear_range(changed_range);

    // Highlight Captures
    TSQueryMatch match;
    uint32_t capture_index;
    while (ts_query_cursor_next_capture(query_cursor, &match, &capture_index)) {
        TSQueryCapture capture = match.captures[capture_index];
        if (capture.index >= capture_count)
            continue;

        QTextCharFormat text_char_format = this->format_lookup_table[capture.index];

        if (true) {  // TODO: skip if text_char_format is the standard format.
            // Single line capture
            if (ts_node_start_point(capture.node).row == ts_node_end_point(capture.node).row) {
                QTextBlock block = this->document->findBlockByNumber(ts_node_start_point(capture.node).row);
                this->apply_format(text_char_format,
                                   block,
                                   ts_node_start_point(capture.node).column / 2,
                                   ts_node_end_point(capture.node).column / 2);
            }
            // Multi line capture
            else if (ts_node_start_point(capture.node).row < ts_node_end_point(capture.node).row) {
                QTextBlock block = this->document->findBlockByNumber(ts_node_start_point(capture.node).row);
                this->apply_format(text_char_format, block, ts_node_start_point(capture.node).column / 2, block.length());
                block = block.next();
                while (block.isValid() && block.blockNumber() < ts_node_end_point(capture.node).row) {
                    this->apply_format(text_char_format, block, 0, block.length());
                    block = block.next();
                }
                if (block.isValid())
                    this->apply_format(text_char_format, block, 0, ts_node_end_point(capture.node).column / 2);
            }
        }
    }

    ts_query_cursor_delete(query_cursor);

    // --- Injections for C Preprocessor Macros ---
    // This allows nested AST parsing (e.g. coloring `for` loops inside `#define FOREACH(...) ...`)
    static TSQuery* inj_query = nullptr;
    if (!inj_query) {
        uint32_t err_offset;
        TSQueryError err_type;
        const char* inj_scm = "(preproc_def value: (preproc_arg) @inj) (preproc_function_def value: (preproc_arg) @inj)";
        inj_query = ts_query_new(this->language, inj_scm, strlen(inj_scm), &err_offset, &err_type);
    }
    
    if (inj_query && !m_performanceMode) {
        TSQueryCursor* cur = ts_query_cursor_new();
        ts_query_cursor_set_byte_range(cur, start_block.position() * 2, (stop_block.position() + stop_block.length()) * 2);
        ts_query_cursor_exec(cur, inj_query, ts_tree_root_node(this->tree));
        
        TSQueryMatch m;
        uint32_t c_idx;
        while (ts_query_cursor_next_capture(cur, &m, &c_idx)) {
            TSQueryCapture cap = m.captures[c_idx];
            
            TSRange range;
            range.start_point = ts_node_start_point(cap.node);
            range.end_point = ts_node_end_point(cap.node);
            range.start_byte = ts_node_start_byte(cap.node);
            range.end_byte = ts_node_end_byte(cap.node);
            
            ts_parser_set_included_ranges(this->parser, &range, 1);
            TSTree* inj_tree = ts_parser_parse_string_encoding(this->parser, NULL, (char*)this->m_sourceCache.utf16(), this->m_sourceCache.length() * 2, TSInputEncodingUTF16LE);
            ts_parser_set_included_ranges(this->parser, NULL, 0); // Reset immediately
            
            if (inj_tree) {
                TSQueryCursor* inner_cur = ts_query_cursor_new();
                ts_query_cursor_exec(inner_cur, this->query, ts_tree_root_node(inj_tree));
                
                TSQueryMatch inner_m;
                uint32_t inner_c_idx;
                while (ts_query_cursor_next_capture(inner_cur, &inner_m, &inner_c_idx)) {
                    TSQueryCapture inner_cap = inner_m.captures[inner_c_idx];
                    if (inner_cap.index >= capture_count)
                        continue;
                    QTextCharFormat fmt = this->format_lookup_table[inner_cap.index];
                    
                    if (ts_node_start_point(inner_cap.node).row == ts_node_end_point(inner_cap.node).row) {
                        QTextBlock block = this->document->findBlockByNumber(ts_node_start_point(inner_cap.node).row);
                        this->apply_format(fmt, block, ts_node_start_point(inner_cap.node).column / 2, ts_node_end_point(inner_cap.node).column / 2);
                    } else if (ts_node_start_point(inner_cap.node).row < ts_node_end_point(inner_cap.node).row) {
                        QTextBlock block = this->document->findBlockByNumber(ts_node_start_point(inner_cap.node).row);
                        this->apply_format(fmt, block, ts_node_start_point(inner_cap.node).column / 2, block.length());
                        block = block.next();
                        while (block.isValid() && block.blockNumber() < ts_node_end_point(inner_cap.node).row) {
                            this->apply_format(fmt, block, 0, block.length());
                            block = block.next();
                        }
                        if (block.isValid())
                            this->apply_format(fmt, block, 0, ts_node_end_point(inner_cap.node).column / 2);
                    }
                }
                ts_query_cursor_delete(inner_cur);
                ts_tree_delete(inj_tree);
            }
        }
        ts_query_cursor_delete(cur);
    }

    if (!m_performanceMode) {
        // Apply rainbow brackets
        // Pre-accumulate bracket depth from root up to start_block so that any
        // unclosed brackets above the visible range are counted. Without this,
        // depth always starts at 0 from start_block and colors shift incorrectly.
        int paren = 0, brace = 0, square = 0;
        {
            TSNode root = ts_tree_root_node(this->tree);
            // Walk the entire tree first to tally all brackets that open before
            // the first block we will colour.
            this->accumulate_bracket_depth(root, paren, brace, square, 0, start_block.blockNumber() - 1);
        }
        this->highlight_rainbow_brackets(ts_tree_root_node(this->tree), paren, brace, square, start_block.blockNumber(), stop_block.blockNumber());
    }

    const int dirtyStart = start_block.position();
    const int dirtyEnd = stop_block.position() + stop_block.length();
    if (dirtyEnd > dirtyStart)
        this->document->markContentsDirty(dirtyStart, dirtyEnd - dirtyStart);
}

void TreeSitterHighlighter::highlight_rainbow_brackets(TSNode node, int& paren, int& brace, int& square, int start_row, int end_row) {
    if ((int)ts_node_end_point(node).row < start_row) return;
    if ((int)ts_node_start_point(node).row > end_row) return;

    if (ts_node_child_count(node) == 0) {
        QString type = QString::fromUtf8(ts_node_type(node));
        int depth = -1;
        if (type == "(") { depth = paren++; }
        else if (type == ")") { depth = --paren; }
        else if (type == "{") { depth = brace++; }
        else if (type == "}") { depth = --brace; }
        else if (type == "[") { depth = square++; }
        else if (type == "]") { depth = --square; }

        if (depth >= 0) {
            int row = ts_node_start_point(node).row;
            if (row >= start_row && row <= end_row) {
                QTextCharFormat fmt;
                if (!m_rainbowColors.isEmpty()) {
                    fmt.setForeground(m_rainbowColors[depth % m_rainbowColors.size()]);
                } else {
                    QColor fallback[] = { Qt::red, QColor(255, 165, 0), Qt::yellow, Qt::green, Qt::cyan, Qt::magenta };
                    fmt.setForeground(fallback[depth % 6]);
                }
                QTextBlock block = this->document->findBlockByNumber(row);
                this->apply_format(fmt, block, ts_node_start_point(node).column / 2, ts_node_end_point(node).column / 2);
            }
        }
    } else {
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            highlight_rainbow_brackets(ts_node_child(node, i), paren, brace, square, start_row, end_row);
        }
    }
}

void TreeSitterHighlighter::accumulate_bracket_depth(TSNode node, int& paren, int& brace, int& square, int start_row, int end_row) {
    if (end_row < 0) return; // Nothing to accumulate
    
    // Stop early if this entire node is after the range we care about
    if ((int)ts_node_start_point(node).row > end_row) {
        return;
    }

    if (ts_node_child_count(node) == 0) {
        int row = ts_node_start_point(node).row;
        if (row >= start_row && row <= end_row) {
            QString type = QString::fromUtf8(ts_node_type(node));
            if (type == "(") { paren++; }
            else if (type == ")") { --paren; }
            else if (type == "{") { brace++; }
            else if (type == "}") { --brace; }
            else if (type == "[") { square++; }
            else if (type == "]") { --square; }
        }
    } else {
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            accumulate_bracket_depth(ts_node_child(node, i), paren, brace, square, start_row, end_row);
        }
    }
}

// Rehighlight the whole document.
void TreeSitterHighlighter::rehighlight() {
    if (!this->document) {
        return;
    }

    BlockRange changed_range = BlockRange(this->document->firstBlock().blockNumber(), this->document->lastBlock().blockNumber());
    this->rehighlight_range(changed_range);
    emit parsed(static_cast<void*>(this->tree));
}

void TreeSitterHighlighter::rehighlightAroundBlock(int centerBlock, int radius)
{
    if (!this->document)
        return;

    const int maxBlock = qMax(0, this->document->blockCount() - 1);
    const int clampedCenter = qBound(0, centerBlock, maxBlock);
    const int clampedRadius = qMax(0, radius);
    const int first = qMax(0, clampedCenter - clampedRadius);
    const int last = qMin(maxBlock, clampedCenter + clampedRadius);
    this->rehighlight_range(BlockRange(first, last));
}

// Get the QTextCharFormat from format_map which shares the longes matching (dot separated) prefix with name.
// In case no key with a shared prefix is found, format_map[""] is returned (defaulting to a new QTextCharFormat if not available).
QTextCharFormat TreeSitterHighlighter::get_format_for_capture_name(std::string name) {
    if (this->format_map.count(name)) {
        return this->format_map.at(name);
    } else if (name.find(".") == std::string::npos) {
        return this->format_map[""];
    } else {
        return this->get_format_for_capture_name(name.substr(0, name.rfind(".")));
    }
}

// Generate a lookup table that maps all query match indices to their QTextCharFormats defined in format_map.
void TreeSitterHighlighter::generate_format_table() {
    delete[] this->format_lookup_table;
    if (!this->query) {
        this->format_lookup_table = new QTextCharFormat[0];
        return;
    }

    uint32_t capture_count = ts_query_capture_count(this->query);
    QTextCharFormat* table = new QTextCharFormat[capture_count];

    for (uint32_t i = 0; i != capture_count; i++) {
        uint32_t length = 0;
        const char* name_ptr = ts_query_capture_name_for_id(this->query, i, &length);
        std::string name = name_ptr ? std::string(name_ptr, length) : std::string();
        table[i] = this->get_format_for_capture_name(name);
    }

    this->format_lookup_table = table;
}

// Apply the the format to block from position start to end.
inline void TreeSitterHighlighter::apply_format(QTextCharFormat format, QTextBlock block, int start, int end) {
    if (!this->document || !block.isValid() || !block.layout())
        return;

    const int textLength = qMax(0, block.length() - 1);
    const int clampedStart = qBound(0, start, textLength);
    const int clampedEnd = qBound(0, end, textLength);
    if (clampedEnd <= clampedStart)
        return;

    QTextLayout::FormatRange r;
    r.start = clampedStart;
    r.length = clampedEnd - clampedStart;
    r.format = format;
    // TODO: Skip preedit area
    QList<QTextLayout::FormatRange> ranges = block.layout()->formats();
    ranges << r;
    block.layout()->setFormats(ranges);
}
