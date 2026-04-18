// ============================================================================
//  FoldQuery.cpp  –  QCodeEngine-C
//
//  ROOT CAUSE FIX (preproc folding):
//    Tree-sitter models the entire  #if … #else … #endif  block as a single
//    preproc_if node whose startRow = line of #if  and  endRow = line of
//    #endif.  FoldManager::rebuildCaches() hides all rows in (start, end),
//    which swallows the #else / #elif lines into the collapsed region —
//    meaning only one fold indicator appeared and the whole block collapsed.
//
//    The fix: when the query cursor yields a preproc_if / preproc_ifdef node,
//    instead of emitting one big range we walk the "alternative" field chain
//    (preproc_if → preproc_elif_clause → … → preproc_else) and emit one
//    INDEPENDENT fold range per branch:
//
//      • #if block :  startRow = row(#if),   endRow = row(#else/#elif)
//      • #else block : startRow = row(#else), endRow = row(#endif)
//
//    Because rebuildCaches hides rows start+1 .. end-1, setting endRow to
//    the row of the NEXT keyword keeps that keyword visible as a fold header
//    in its own right.  Every branch becomes independently collapsible.
// ============================================================================
#include "CodeEditor/FoldQuery.h"
#include <QDebug>
#include <cstring>
#include <algorithm>

// ============================================================================
//  Internal helpers  (file-scope, not members — keep the header clean)
// ============================================================================

// ---------------------------------------------------------------------------
//  addPreprocessorChainFolds
//
//  Recurses through the alternative chain of a preprocessor conditional and
//  pushes one FoldRange per branch.
//
//  node         — current node (preproc_if, preproc_ifdef, preproc_elif_clause,
//                 or preproc_else)
//  alt          — the "alternative" child of `node`  (guaranteed non-null)
//  rootEndRow   — row of the #endif that closes the outermost conditional
//  rootEndByte  — byte offset of that same #endif
//  out          — accumulator
// ---------------------------------------------------------------------------
static void addPreprocessorChainFolds(TSNode  node,
                                      TSNode  alt,
                                      uint32_t rootEndRow,
                                      uint32_t rootEndByte,
                                      std::vector<FoldRange>& out)
{
    const TSPoint nodeStart = ts_node_start_point(node);
    const TSPoint altStart  = ts_node_start_point(alt);

    // ── Emit a range for this branch ────────────────────────────────────────
    // endRow = row of the NEXT keyword (#else / #elif).
    // rebuildCaches hides  (startRow, endRow)  exclusive on both ends, so:
    //   • the #if / #elif header line  stays visible  (= startRow)
    //   • the #else / #elif line       stays visible  (= endRow, not hidden)
    if (altStart.row > nodeStart.row) {
        FoldRange fr;
        fr.startRow  = nodeStart.row;
        fr.endRow    = altStart.row;      // next keyword line — NOT hidden
        fr.startByte = ts_node_start_byte(node);
        fr.endByte   = ts_node_start_byte(alt);
        fr.collapsed = false;
        out.push_back(fr);
    }

    // ── Recurse into the alternative ────────────────────────────────────────
    static constexpr const char kAlt[] = "alternative";
    TSNode nextAlt = ts_node_child_by_field_name(alt, kAlt, sizeof(kAlt) - 1);

    if (ts_node_is_null(nextAlt)) {
        // `alt` is the terminal node — either preproc_else (which never has a
        // further alternative) or the last preproc_elif_clause.
        // Its range runs from altStart all the way to the closing #endif.
        const TSPoint altStartPt = ts_node_start_point(alt);
        if (rootEndRow > altStartPt.row) {
            FoldRange fr;
            fr.startRow  = altStartPt.row;
            fr.endRow    = rootEndRow;   // #endif line stays visible
            fr.startByte = ts_node_start_byte(alt);
            fr.endByte   = rootEndByte;
            fr.collapsed = false;
            out.push_back(fr);
        }
    } else {
        // Another #elif further along — keep recursing.
        addPreprocessorChainFolds(alt, nextAlt, rootEndRow, rootEndByte, out);
    }
}

// ---------------------------------------------------------------------------
//  splitPreprocessorFolds
//
//  Entry point for preproc_if / preproc_ifdef / preproc_ifndef nodes found
//  by the query cursor.  If the node has no alternatives it emits one range
//  (the simple #if … #endif case).  Otherwise it delegates to the chain
//  helper so each branch gets its own independent fold.
// ---------------------------------------------------------------------------
static void splitPreprocessorFolds(TSNode node, std::vector<FoldRange>& out)
{
    const TSPoint nodeStart = ts_node_start_point(node);
    const TSPoint nodeEnd   = ts_node_end_point(node);

    if (nodeEnd.row <= nodeStart.row) return;   // single-line — nothing to fold

    static constexpr const char kAlt[] = "alternative";
    TSNode alt = ts_node_child_by_field_name(node, kAlt, sizeof(kAlt) - 1);

    if (ts_node_is_null(alt)) {
        // Simple  #if … #endif  (or #ifdef without #else).
        // Emit one range covering the whole block.
        FoldRange fr;
        fr.startRow  = nodeStart.row;
        fr.endRow    = nodeEnd.row;
        fr.startByte = ts_node_start_byte(node);
        fr.endByte   = ts_node_end_byte(node);
        fr.collapsed = false;
        out.push_back(fr);
    } else {
        // Has at least one #else / #elif — split into independent ranges.
        addPreprocessorChainFolds(node, alt,
                                  nodeEnd.row,
                                  ts_node_end_byte(node),
                                  out);
    }
}

// ============================================================================
//  FoldQuery  (public API — unchanged interface)
// ============================================================================

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

FoldQuery::~FoldQuery()
{
    if (m_query) {
        ts_query_delete(m_query);
        m_query = nullptr;
    }
}

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
        if (m_captureFound && captureIndex != m_foldCaptureIndex) continue;

        const TSNode node = match.captures[captureIndex].node;
        if (ts_node_is_error(node) || ts_node_is_missing(node)) continue;

        const char* nodeType = ts_node_type(node);

        // ── Preprocessor conditionals — split at #else / #elif ────────────
        //
        // tree-sitter gives us ONE node spanning the entire #if...#endif.
        // We intercept it here and delegate to splitPreprocessorFolds which
        // walks the "alternative" field chain and emits one FoldRange per
        // branch.  This means #else and #elif blocks get their own fold
        // indicators and collapse independently.
        //
        // preproc_elif_clause is NOT in the scheme; the chain helper emits
        // those ranges internally, so we never see one here.
        if (std::strcmp(nodeType, "preproc_if")    == 0 ||
            std::strcmp(nodeType, "preproc_ifdef")  == 0 ||
            std::strcmp(nodeType, "preproc_ifndef") == 0)
        {
            splitPreprocessorFolds(node, raw);
            continue;                   // do NOT fall through to generic path
        }

        // ── All other nodes (compound_statement, struct bodies, etc.) ──────
        const TSPoint start = ts_node_start_point(node);
        const TSPoint end   = ts_node_end_point(node);
        if (end.row <= start.row) continue;     // single-line — skip

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

    // Sort: startRow ascending, endRow descending (largest span first).
    std::sort(raw.begin(), raw.end(), [](const FoldRange& a, const FoldRange& b) {
        if (a.startRow != b.startRow) return a.startRow < b.startRow;
        return a.endRow > b.endRow;
    });

    // Deduplicate: per unique startRow keep only the first (= largest) entry.
    std::vector<FoldRange> result;
    result.reserve(raw.size());
    for (const FoldRange& fr : raw) {
        if (result.empty() || result.back().startRow != fr.startRow)
            result.push_back(fr);
    }

    return result;
}
