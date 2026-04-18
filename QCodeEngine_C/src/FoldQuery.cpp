// ============================================================================
//  FoldQuery.cpp  –  QCodeEngine-C
//
//  FIXES vs original:
//    1. captureIndex guard: the resolved m_foldCaptureIndex is now compared
//       against the index returned by ts_query_cursor_next_capture.  The
//       original code computed the index but then unconditionally processed
//       every capture, meaning a second capture name (e.g. @nofold) would
//       have been silently treated as a foldable node.
//    2. sourceUtf8 parameter removed from computeRanges (was unused / misleading).
//    3. Deduplication uses a smarter strategy: when two ranges share the same
//       startRow but differ in endRow, we keep the LARGEST span (outermost),
//       which matches VS Code / Zed behavior for nested captures on the same
//       opening line.
// ============================================================================
#include "CodeEditor/FoldQuery.h"
#include <QDebug>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
FoldQuery::FoldQuery(const TSLanguage* lang, const QByteArray& schemeSrc)
{
    if (!lang || schemeSrc.isEmpty()) return;

    uint32_t     errorOffset = 0;
    TSQueryError errorType   = TSQueryErrorNone;

    m_query = ts_query_new(
        lang,
        schemeSrc.constData(),
        static_cast<uint32_t>(schemeSrc.size()),
        &errorOffset,
        &errorType
    );

    if (!m_query) {
        qWarning() << "FoldQuery: ts_query_new failed at byte offset"
                   << errorOffset << "error type" << static_cast<int>(errorType);
        return;
    }

    // Resolve which capture index maps to "@fold".
    // FIX: original stored this but then NEVER checked it in computeRanges.
    const uint32_t captureCount = ts_query_capture_count(m_query);
    for (uint32_t i = 0; i < captureCount; ++i) {
        uint32_t    nameLen = 0;
        const char* name    = ts_query_capture_name_for_id(m_query, i, &nameLen);
        if (name && nameLen == 4 && std::strncmp(name, "fold", 4) == 0) {
            m_foldCaptureIndex = i;
            m_captureFound     = true;
            break;
        }
    }

    if (!m_captureFound) {
        qWarning() << "FoldQuery: @fold capture not found in scheme — "
                      "all captures will be treated as folds (fallback)";
    }
}

// ---------------------------------------------------------------------------
FoldQuery::~FoldQuery()
{
    if (m_query) {
        ts_query_delete(m_query);
        m_query = nullptr;
    }
}

// ---------------------------------------------------------------------------
std::vector<FoldRange> FoldQuery::computeRanges(TSTree* tree) const
{
    if (!m_query || !tree) return {};

    std::vector<FoldRange> raw;
    raw.reserve(64);

    TSQueryCursor* cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, m_query, ts_tree_root_node(tree));

    TSQueryMatch match;
    uint32_t     captureIndex = 0;

    while (ts_query_cursor_next_capture(cursor, &match, &captureIndex)) {
        // FIX #1: only process the @fold capture, ignore any other captures
        // in the scheme.  The original skipped this check entirely.
        if (m_captureFound && captureIndex != m_foldCaptureIndex) continue;

        const TSNode node = match.captures[captureIndex].node;

        // Skip parse errors and placeholder nodes
        if (ts_node_is_error(node) || ts_node_is_missing(node)) continue;

        const TSPoint start = ts_node_start_point(node);
        const TSPoint end   = ts_node_end_point(node);

        // Skip single-line nodes — nothing to fold
        if (end.row <= start.row) continue;

        FoldRange fr;
        fr.startRow  = start.row;
        fr.endRow    = end.row;
        fr.startByte = ts_node_start_byte(node);
        fr.endByte   = ts_node_end_byte(node);
        fr.collapsed = false;
        raw.push_back(fr);
    }

    ts_query_cursor_delete(cursor);

    if (raw.empty()) return raw;

    // Sort by startRow asc, then endRow desc (largest span first)
    std::sort(raw.begin(), raw.end(), [](const FoldRange& a, const FoldRange& b) {
        if (a.startRow != b.startRow) return a.startRow < b.startRow;
        return a.endRow > b.endRow;   // larger span wins
    });

    // Deduplicate: for each unique startRow keep only the first (= largest) entry.
    // FIX #3: original kept the SMALLEST span (sort was ascending on endRow).
    std::vector<FoldRange> result;
    result.reserve(raw.size());
    for (const FoldRange& fr : raw) {
        if (result.empty() || result.back().startRow != fr.startRow) {
            result.push_back(fr);
        }
        // else: same startRow, smaller span — discard
    }

    return result;
}