#pragma once
// ============================================================================
//  FoldQuery.h  –  QCodeEngine-C
//
//  Wraps a TSQuery that captures @fold nodes from C source.
//  Computes a sorted, deduplicated list of FoldRange values from a TSTree.
//
//  IMPROVEMENTS over original:
//    • m_foldCaptureIndex is now actually USED inside computeRanges (was
//      computed but then ignored — any extra capture name would have been
//      incorrectly treated as a fold).
//    • sourceUtf8 parameter removed from computeRanges — it was unused and
//      misleading.
//    • FOLD_SCHEME exposed as a static constexpr so FoldManager, tests, and
//      the QRC resource can all reference a single source of truth instead
//      of maintaining three diverging copies.
// ============================================================================

#include <vector>
#include <cstdint>
#include <QByteArray>
#include <tree_sitter/api.h>

// ---------------------------------------------------------------------------
//  FoldRange
//  All row values are 0-based (tree-sitter TSPoint.row convention).
//  CodeEditor / GutterWidget convert to 1-based at the widget boundary.
// ---------------------------------------------------------------------------
struct FoldRange {
    uint32_t startByte = 0;
    uint32_t endByte   = 0;
    uint32_t startRow  = 0;   // 0-based line of the opening delimiter
    uint32_t endRow    = 0;   // 0-based line of the closing delimiter
    bool     collapsed = false;
};

// ---------------------------------------------------------------------------
//  FoldQuery
// ---------------------------------------------------------------------------
class FoldQuery {
public:
    // Canonical fold scheme for C — single source of truth.
    // FoldManager and unit tests both reference this constant; the dead
    // c_folds.scm in the QRC can be removed or replaced with a #include.
    static constexpr const char* FOLD_SCHEME = R"SCHEME(
; Compound blocks: function bodies, if/for/while/switch/do
(compound_statement) @fold

; Struct / union / enum bodies
(struct_specifier  body: (field_declaration_list) @fold)
(union_specifier   body: (field_declaration_list) @fold)
(enum_specifier    body: (enumerator_list)        @fold)

; Block comments  /* ... */
(comment) @fold

; Preprocessor conditional blocks
(preproc_ifdef) @fold
(preproc_if)    @fold
(preproc_elif)  @fold
)SCHEME";

    explicit FoldQuery(const TSLanguage* lang,
                       const QByteArray& schemeSrc = QByteArray(FOLD_SCHEME));
    ~FoldQuery();

    FoldQuery(const FoldQuery&)            = delete;
    FoldQuery& operator=(const FoldQuery&) = delete;

    bool isValid() const { return m_query != nullptr; }

    // Returns a sorted, deduplicated vector of fold ranges.
    // Single-line nodes (startRow == endRow) are discarded.
    // Error / missing nodes are discarded.
    std::vector<FoldRange> computeRanges(TSTree* tree) const;

private:
    TSQuery* m_query            = nullptr;
    uint32_t m_foldCaptureIndex = 0;    // resolved index of the "@fold" name
    bool     m_captureFound     = false; // false → treat index 0 as fallback
};