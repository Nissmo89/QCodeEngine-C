#include "CodeEditor/FoldQuery.h"

#include <QDebug>
#include <cstring>
#include <algorithm>

FoldQuery::FoldQuery(const TSLanguage* lang, const QByteArray& schemeSrc)
{
    uint32_t errorOffset = 0;
    TSQueryError errorType = TSQueryErrorNone;

    m_query = ts_query_new(
        lang,
        schemeSrc.constData(),
        static_cast<uint32_t>(schemeSrc.size()),
        &errorOffset,
        &errorType
    );

    if (!m_query) {
        qWarning() << "FoldQuery: ts_query_new failed at offset" << errorOffset
                   << "error type" << static_cast<int>(errorType);
        return;
    }

    // Resolve the @fold capture index
    uint32_t captureCount = ts_query_capture_count(m_query);
    m_foldCaptureIndex = 0;
    for (uint32_t i = 0; i < captureCount; ++i) {
        uint32_t nameLen = 0;
        const char* name = ts_query_capture_name_for_id(m_query, i, &nameLen);
        if (name && std::strncmp(name, "fold", nameLen) == 0) {
            m_foldCaptureIndex = i;
            break;
        }
    }
}

FoldQuery::~FoldQuery()
{
    if (m_query) {
        ts_query_delete(m_query);
        m_query = nullptr;
    }
}

bool FoldQuery::isValid() const
{
    return m_query != nullptr;
}

std::vector<FoldRange> FoldQuery::computeRanges(TSTree* tree,
                                                 const QByteArray& /*sourceUtf8*/) const
{
    if (!m_query || !tree) {
        return {};
    }

    std::vector<FoldRange> ranges;

    TSQueryCursor* cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, m_query, ts_tree_root_node(tree));

    TSQueryMatch match;
    uint32_t captureIndex = 0;

    while (ts_query_cursor_next_capture(cursor, &match, &captureIndex)) {
        TSNode node = match.captures[captureIndex].node;

        if (ts_node_is_error(node) || ts_node_is_missing(node)) {
            continue;
        }

        TSPoint startPoint = ts_node_start_point(node);
        TSPoint endPoint   = ts_node_end_point(node);

        // Skip single-line folds (must span at least 2 lines)
        if (endPoint.row <= startPoint.row) {
            continue;
        }

        FoldRange fr;
        fr.startRow  = startPoint.row;
        fr.endRow    = endPoint.row;
        fr.startByte = ts_node_start_byte(node);
        fr.endByte   = ts_node_end_byte(node);
        fr.collapsed = false;

        ranges.push_back(fr);
    }

    ts_query_cursor_delete(cursor);

    // Remove duplicate ranges (same start and end row)
    // Sort by start row, then by end row
    std::sort(ranges.begin(), ranges.end(), [](const FoldRange& a, const FoldRange& b) {
        if (a.startRow != b.startRow) {
            return a.startRow < b.startRow;
        }
        return a.endRow < b.endRow;
    });

    // Remove exact duplicates only (same start AND end row)
    std::vector<FoldRange> filtered;
    for (size_t i = 0; i < ranges.size(); ++i) {
        // Check if this is a duplicate of the previous range
        if (i > 0 && 
            ranges[i].startRow == ranges[i-1].startRow && 
            ranges[i].endRow == ranges[i-1].endRow) {
            continue; // Skip duplicate
        }
        filtered.push_back(ranges[i]);
    }

    return filtered;
}
