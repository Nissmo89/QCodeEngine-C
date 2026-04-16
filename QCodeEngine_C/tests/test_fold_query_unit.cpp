// Feature: zed-style-code-folding
// Unit tests for FoldQuery
// Validates: Requirements 2.1, 2.2, 2.3, 2.5, 2.6

#include <QtTest/QtTest>
#include <QByteArray>
#include <QString>
#include <vector>

#include "CodeEditor/FoldQuery.h"

// tree-sitter C language declaration
extern "C" {
    const TSLanguage* tree_sitter_c(void);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char* VALID_FOLD_SCHEME = R"(
(compound_statement) @fold

(struct_specifier
  body: (field_declaration_list) @fold)

(union_specifier
  body: (field_declaration_list) @fold)

(enum_specifier
  body: (enumerator_list) @fold)

(comment) @fold

(preproc_ifdef)  @fold
(preproc_if)     @fold
(preproc_elif)   @fold
)";

// Parse source with tree-sitter and return the tree (caller owns it).
static TSTree* parseSource(const QByteArray& src)
{
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_c());
    TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                          src.constData(),
                                          static_cast<uint32_t>(src.size()));
    ts_parser_delete(parser);
    return tree;
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestFoldQueryUnit : public QObject
{
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------------
    // 1. Construct with valid language + valid .scm → isValid() == true
    //    Validates: Requirements 2.1, 2.3
    // -----------------------------------------------------------------------
    void construct_validLanguageAndScheme_isValidTrue()
    {
        FoldQuery query(tree_sitter_c(), QByteArray(VALID_FOLD_SCHEME));
        QVERIFY2(query.isValid(),
                 "FoldQuery constructed with a valid language and valid .scm "
                 "must report isValid() == true");
    }

    // -----------------------------------------------------------------------
    // 2. Construct with invalid .scm → isValid() == false
    //    Validates: Requirements 2.2, 2.3
    //    Uses a syntactically invalid scheme string (unclosed paren).
    // -----------------------------------------------------------------------
    void construct_invalidScheme_isValidFalse()
    {
        // Unclosed parenthesis — ts_query_new must fail
        const QByteArray invalidScheme("(invalid_syntax @fold");
        FoldQuery query(tree_sitter_c(), invalidScheme);
        QVERIFY2(!query.isValid(),
                 "FoldQuery constructed with a syntactically invalid .scm "
                 "must report isValid() == false");
    }

    // -----------------------------------------------------------------------
    // 3. computeRanges on empty source → returns empty vector
    //    Validates: Requirements 2.4, 2.5
    // -----------------------------------------------------------------------
    void computeRanges_emptySource_returnsEmpty()
    {
        FoldQuery query(tree_sitter_c(), QByteArray(VALID_FOLD_SCHEME));
        QVERIFY(query.isValid());

        QByteArray emptySource("");
        TSTree* tree = parseSource(emptySource);
        QVERIFY2(tree != nullptr, "tree-sitter must parse an empty string without crashing");

        std::vector<FoldRange> ranges = query.computeRanges(tree, emptySource);
        ts_tree_delete(tree);

        QVERIFY2(ranges.empty(),
                 "computeRanges on an empty source must return an empty vector");
    }

    // -----------------------------------------------------------------------
    // 4. computeRanges skips single-line nodes
    //    Validates: Requirements 2.6
    //    Uses a struct entirely on one line — no fold range should be produced.
    // -----------------------------------------------------------------------
    void computeRanges_singleLineConstruct_noFoldRange()
    {
        FoldQuery query(tree_sitter_c(), QByteArray(VALID_FOLD_SCHEME));
        QVERIFY(query.isValid());

        // All on one line: tree-sitter will parse a struct_specifier whose
        // field_declaration_list starts and ends on row 0 → must be skipped.
        QByteArray src("struct S { int x; };");
        TSTree* tree = parseSource(src);
        QVERIFY2(tree != nullptr, "tree-sitter must parse the single-line struct");

        std::vector<FoldRange> ranges = query.computeRanges(tree, src);
        ts_tree_delete(tree);

        QVERIFY2(ranges.empty(),
                 "computeRanges must not produce any FoldRange for a construct "
                 "that starts and ends on the same line");
    }
};

QTEST_MAIN(TestFoldQueryUnit)
#include "test_fold_query_unit.moc"
