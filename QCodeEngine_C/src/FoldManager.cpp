#include "FoldManager.h"

#include <QFile>
#include <QDebug>
#include <algorithm>

// Declare the tree-sitter C language function
extern "C" {
    const TSLanguage* tree_sitter_c(void);
}

FoldManager::FoldManager(QObject* parent)
    : QObject(parent)
{
    // Create the parser
    m_parser = ts_parser_new();
    if (!m_parser) {
        qCritical() << "FoldManager: ts_parser_new() returned null";
        return;
    }

    // Set the C language
    if (!ts_parser_set_language(m_parser, tree_sitter_c())) {
        qCritical() << "FoldManager: ts_parser_set_language() failed";
        ts_parser_delete(m_parser);
        m_parser = nullptr;
        return;
    }

    // Embedded fold query scheme
    const char* scheme_str = R"(
; Function bodies, if/for/while/switch/do blocks
(compound_statement) @fold

; Struct, union, enum bodies
(struct_specifier
  body: (field_declaration_list) @fold)

(union_specifier
  body: (field_declaration_list) @fold)

(enum_specifier
  body: (enumerator_list) @fold)

; Block comments  /* ... */
(comment) @fold

; Preprocessor conditional blocks
(preproc_ifdef)  @fold
(preproc_if)     @fold
(preproc_elif)   @fold
)";
    QByteArray scheme(scheme_str);

    // Construct the FoldQuery
    m_query = std::make_unique<FoldQuery>(tree_sitter_c(), scheme);
    if (!m_query->isValid()) {
        qCritical() << "FoldManager: FoldQuery construction failed (invalid query)";
        // Leave m_query set but invalid; reparse will be a no-op
    }
}

FoldManager::~FoldManager()
{
    if (m_tree) {
        ts_tree_delete(m_tree);
        m_tree = nullptr;
    }
    if (m_parser) {
        ts_parser_delete(m_parser);
        m_parser = nullptr;
    }
}

void FoldManager::reparse(const QString& source)
{
    // No-op if parser failed to initialise
    if (!m_parser) {
        return;
    }

    // Empty-source fast path
    if (source.isEmpty()) {
        if (m_tree) {
            ts_tree_delete(m_tree);
            m_tree = nullptr;
        }
        m_ranges.clear();
        emit foldsChanged();
        return;
    }

    // Convert to UTF-8 for tree-sitter
    QByteArray utf8 = source.toUtf8();

    // Incremental parse (pass old tree for reuse)
    TSTree* newTree = ts_parser_parse_string(
        m_parser,
        m_tree,
        utf8.constData(),
        static_cast<uint32_t>(utf8.size())
    );

    // Delete old tree and store new one
    if (m_tree) {
        ts_tree_delete(m_tree);
    }
    m_tree = newTree;

    // Compute new ranges (empty if query is invalid or tree is null)
    std::vector<FoldRange> newRanges;
    if (m_tree && m_query && m_query->isValid()) {
        newRanges = m_query->computeRanges(m_tree, utf8);
    }

    // Preserve collapsed flags from old ranges by matching startRow
    for (FoldRange& nr : newRanges) {
        auto it = std::find_if(m_ranges.begin(), m_ranges.end(),
            [&nr](const FoldRange& old) {
                return old.startRow == nr.startRow;
            });
        if (it != m_ranges.end() && it->collapsed) {
            nr.collapsed = true;
        }
    }

    m_ranges = std::move(newRanges);
    emit foldsChanged();
}

bool FoldManager::hasFoldAt(int line) const
{
    for (const FoldRange& fr : m_ranges) {
        if (static_cast<int>(fr.startRow) == line) {
            return true;
        }
    }
    return false;
}

bool FoldManager::isLineHidden(int line) const
{
    for (const FoldRange& fr : m_ranges) {
        if (fr.collapsed &&
            line > static_cast<int>(fr.startRow) &&
            line < static_cast<int>(fr.endRow)) {
            return true;
        }
    }
    return false;
}

void FoldManager::toggleFold(int line)
{
    for (FoldRange& fr : m_ranges) {
        if (static_cast<int>(fr.startRow) == line) {
            fr.collapsed = !fr.collapsed;
            emit foldsChanged();
            return;
        }
    }
    // No matching range — no-op, no signal
}

const FoldRange* FoldManager::foldAt(int line) const
{
    for (const FoldRange& fr : m_ranges) {
        if (static_cast<int>(fr.startRow) == line) {
            return &fr;
        }
    }
    return nullptr;
}

const std::vector<FoldRange>& FoldManager::ranges() const
{
    return m_ranges;
}
