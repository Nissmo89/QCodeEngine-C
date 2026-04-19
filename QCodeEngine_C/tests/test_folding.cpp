// ============================================================================
//  test_folding.cpp  –  QCodeEngine-C  [NEW]
//
//  Qt Test suite for FoldManager and FoldQuery.
//  Build target: add_editor_test(test_folding) in CMakeLists.txt.
//
//  Test groups:
//    1. FoldQuery — scheme validity, basic range detection
//    2. FoldQuery — preprocessor chain splitting
//    3. FoldQuery — depth-limit guard (FIX 5)
//    4. FoldManager — toggle, caches, dirty range (FIX 1)
//    5. FoldManager — foldAll / unfoldAll diff (FIX 2)
//    6. FoldManager — collapse state persistence (FIX 3)
//    7. FoldManager — collapse state survives re-parse
// ============================================================================
#include <QtTest>
#include <QTextDocument>

// Pull in the units under test directly (no installed headers needed).
#include "../src/FoldManager.h"
#include "../src/FoldManager.cpp"   // single-TU build for tests
#include "../src/FoldQuery.cpp"

extern "C" { const TSLanguage* tree_sitter_c(void); }

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
namespace {

// Build a TSTree from a C source string.  Caller owns the returned tree.
TSTree* parseC(const char* src)
{
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_c());
    TSTree* tree = ts_parser_parse_string(parser, nullptr, src, (uint32_t)strlen(src));
    ts_parser_delete(parser);
    return tree;
}

// Convenience: returns FoldManager wired to a QTextDocument pre-loaded
// with `src`, with its ranges already populated from a fresh parse.
struct Fixture {
    QTextDocument       doc;
    FoldManager         fm;
    TSParser*           parser = nullptr;
    TSTree*             tree   = nullptr;

    explicit Fixture(const char* src)
    {
        doc.setPlainText(QString::fromUtf8(src));
        fm.setDocument(&doc);

        parser = ts_parser_new();
        ts_parser_set_language(parser, tree_sitter_c());
        tree = ts_parser_parse_string(parser, nullptr, src, (uint32_t)strlen(src));
        fm.updateFoldRanges(tree, &doc);
    }

    ~Fixture()
    {
        ts_tree_delete(tree);
        ts_parser_delete(parser);
    }
};

} // namespace

// ============================================================================
//  Test class
// ============================================================================
class TestFolding : public QObject
{
    Q_OBJECT

private slots:

    // ── Group 1: FoldQuery — scheme validity ─────────────────────────────────

    void test_foldQuery_scheme_is_valid()
    {
        FoldQuery q(tree_sitter_c());
        QVERIFY(q.isValid());
    }

    void test_foldQuery_detects_compound_statement()
    {
        const char* src =
            "void foo() {\n"
            "    int x = 1;\n"
            "}\n";

        TSTree* tree = parseC(src);
        FoldQuery q(tree_sitter_c());
        auto ranges = q.computeRanges(tree);
        ts_tree_delete(tree);

        // Must have at least one range starting at row 0 (the function body)
        QVERIFY(!ranges.empty());
        bool found = false;
        for (auto& r : ranges)
            if (r.startRow == 0) { found = true; break; }
        QVERIFY(found);
    }

    void test_foldQuery_single_line_node_skipped()
    {
        const char* src = "void foo() { int x = 1; }\n";
        TSTree* tree = parseC(src);
        FoldQuery q(tree_sitter_c());
        auto ranges = q.computeRanges(tree);
        ts_tree_delete(tree);

        // single-line compound_statement must be discarded
        for (auto& r : ranges)
            QVERIFY2(r.startRow != r.endRow, "single-line range leaked through");
    }

    void test_foldQuery_struct_body_detected()
    {
        const char* src =
            "struct Point {\n"
            "    int x;\n"
            "    int y;\n"
            "};\n";

        TSTree* tree = parseC(src);
        FoldQuery q(tree_sitter_c());
        auto ranges = q.computeRanges(tree);
        ts_tree_delete(tree);

        QVERIFY(!ranges.empty());
    }

    // FIX 4 regression: return_statement must NOT appear as a fold range
    void test_foldQuery_return_statement_not_folded()
    {
        const char* src =
            "int add(int a, int b) {\n"
            "    return a + b;\n"
            "}\n";

        TSTree* tree = parseC(src);
        FoldQuery q(tree_sitter_c());
        auto ranges = q.computeRanges(tree);
        ts_tree_delete(tree);

        for (auto& r : ranges) {
            // Row 1 is the return_statement line — must not be a fold START
            QVERIFY2(r.startRow != 1, "return_statement should not be a fold start");
        }
    }

    // FIX 4 regression: parenthesized_expression must NOT appear
    void test_foldQuery_parenthesized_expr_not_folded()
    {
        const char* src =
            "int f(int x) {\n"
            "    return (x * 2\n"
            "            + 1);\n"
            "}\n";

        TSTree* tree = parseC(src);
        FoldQuery q(tree_sitter_c());
        auto ranges = q.computeRanges(tree);
        ts_tree_delete(tree);

        for (auto& r : ranges) {
            QVERIFY2(r.startRow != 1,
                     "parenthesized_expression on row 1 should not be a fold");
        }
    }

    // ── Group 2: FoldQuery — preprocessor chain splitting ────────────────────

    void test_foldQuery_preproc_if_else_split_into_two_ranges()
    {
        const char* src =
            "#if PLATFORM_WIN\n"       // row 0
            "int win_code = 1;\n"       // row 1
            "#else\n"                   // row 2
            "int other_code = 2;\n"     // row 3
            "#endif\n";                 // row 4

        TSTree* tree = parseC(src);
        FoldQuery q(tree_sitter_c());
        auto ranges = q.computeRanges(tree);
        ts_tree_delete(tree);

        // Must produce two independent fold ranges, not one big one
        QCOMPARE((int)ranges.size(), 2);
        QCOMPARE((int)ranges[0].startRow, 0);
        QCOMPARE((int)ranges[0].endRow,   2);   // ends at #else line
        QCOMPARE((int)ranges[1].startRow, 2);
        QCOMPARE((int)ranges[1].endRow,   4);   // ends at #endif
    }

    void test_foldQuery_preproc_if_no_else_single_range()
    {
        const char* src =
            "#ifdef DEBUG\n"            // row 0
            "void debug_log() {}\n"     // row 1
            "#endif\n";                 // row 2

        TSTree* tree = parseC(src);
        FoldQuery q(tree_sitter_c());
        auto ranges = q.computeRanges(tree);
        ts_tree_delete(tree);

        bool found = false;
        for (auto& r : ranges)
            if (r.startRow == 0 && r.endRow == 2) { found = true; break; }
        QVERIFY(found);
    }

    void test_foldQuery_preproc_elif_three_branches()
    {
        const char* src =
            "#if A\n"         // row 0
            "int a;\n"        // row 1
            "#elif B\n"       // row 2
            "int b;\n"        // row 3
            "#else\n"         // row 4
            "int c;\n"        // row 5
            "#endif\n";       // row 6

        TSTree* tree = parseC(src);
        FoldQuery q(tree_sitter_c());
        auto ranges = q.computeRanges(tree);
        ts_tree_delete(tree);

        // Expect three independent ranges: [0,2], [2,4], [4,6]
        QCOMPARE((int)ranges.size(), 3);
        QCOMPARE((int)ranges[0].startRow, 0);  QCOMPARE((int)ranges[0].endRow, 2);
        QCOMPARE((int)ranges[1].startRow, 2);  QCOMPARE((int)ranges[1].endRow, 4);
        QCOMPARE((int)ranges[2].startRow, 4);  QCOMPARE((int)ranges[2].endRow, 6);
    }

    // ── Group 3: FIX 5 — depth-limit guard ───────────────────────────────────

    void test_foldQuery_deep_elif_chain_does_not_crash()
    {
        // Build a C source with MAX_PREPROC_DEPTH + 10 #elif branches.
        // This must not crash (stack overflow) and must return at least
        // one fold range.
        const int N = FoldQuery::MAX_PREPROC_DEPTH + 10;
        QByteArray src;
        src += "#if BRANCH_0\nint v0;\n";
        for (int i = 1; i < N; ++i)
            src += QByteArray("#elif BRANCH_") + QByteArray::number(i)
                   + "\nint v" + QByteArray::number(i) + ";\n";
        src += "#else\nint velse;\n#endif\n";

        TSTree* tree = parseC(src.constData());
        FoldQuery q(tree_sitter_c());
        auto ranges = q.computeRanges(tree);   // must not crash
        ts_tree_delete(tree);

        QVERIFY(!ranges.empty());
    }

    // ── Group 4: FoldManager — toggle and caches ─────────────────────────────

    void test_foldManager_toggleFold_hides_inner_lines()
    {
        Fixture f(
            "void foo() {\n"    // row 0
            "    int x = 1;\n"  // row 1
            "    int y = 2;\n"  // row 2
            "}\n"               // row 3
        );

        // Row 0 should be a fold header
        QVERIFY(f.fm.isFolded(0) == false);         // not collapsed yet
        QVERIFY(f.fm.isLineHidden(1) == false);

        f.fm.toggleFold(0);

        QVERIFY(f.fm.isFolded(0) == true);
        QVERIFY(f.fm.isLineHidden(1) == true);
        QVERIFY(f.fm.isLineHidden(2) == true);
        QVERIFY(f.fm.isLineHidden(3) == false);    // closing brace stays visible
    }

    void test_foldManager_toggleFold_twice_restores()
    {
        Fixture f(
            "void bar() {\n"
            "    return 0;\n"
            "}\n"
        );

        f.fm.toggleFold(0);
        QVERIFY(f.fm.isFolded(0));

        f.fm.toggleFold(0);
        QVERIFY(!f.fm.isFolded(0));
        QVERIFY(!f.fm.isLineHidden(1));
    }

    void test_foldManager_toggle_nonexistent_header_is_noop()
    {
        Fixture f("int x = 1;\n");
        // Line 0 has no fold header — must be a safe no-op
        f.fm.toggleFold(0);
        QVERIFY(!f.fm.isFolded(0));
    }

    void test_foldManager_findFoldContaining_returns_correct_start()
    {
        Fixture f(
            "void foo() {\n"    // row 0  ← fold start
            "    int x = 1;\n"  // row 1
            "    int y = 2;\n"  // row 2
            "}\n"               // row 3
        );
        f.fm.toggleFold(0);
        QCOMPARE(f.fm.findFoldContaining(1), 0);
        QCOMPARE(f.fm.findFoldContaining(2), 0);
        QCOMPARE(f.fm.findFoldContaining(3), -1); // closing brace not hidden
    }

    // ── Group 5: FoldManager — foldAll / unfoldAll ───────────────────────────

    void test_foldManager_foldAll_collapses_all()
    {
        Fixture f(
            "void foo() {\n"
            "    int x;\n"
            "}\n"
            "void bar() {\n"
            "    int y;\n"
            "}\n"
        );

        f.fm.foldAll();

        const auto& ranges = f.fm.foldRanges();
        for (auto it = ranges.begin(); it != ranges.end(); ++it)
            QVERIFY(f.fm.isFolded(it.key()));
    }

    void test_foldManager_unfoldAll_clears_all()
    {
        Fixture f(
            "void foo() {\n"
            "    int x;\n"
            "}\n"
            "void bar() {\n"
            "    int y;\n"
            "}\n"
        );

        f.fm.foldAll();
        f.fm.unfoldAll();

        const auto& ranges = f.fm.foldRanges();
        for (auto it = ranges.begin(); it != ranges.end(); ++it)
            QVERIFY(!f.fm.isFolded(it.key()));
    }

    // ── Group 6: FoldManager — persistence (FIX 3) ───────────────────────────

    void test_foldManager_saveRestoreCollapsedState_roundtrip()
    {
        const char* src =
            "void foo() {\n"
            "    int x;\n"
            "}\n";

        Fixture f(src);
        f.fm.toggleFold(0);
        QVERIFY(f.fm.isFolded(0));

        // Save
        QVector<int> saved = f.fm.saveCollapsedState();
        QVERIFY(saved.contains(0));

        // Simulate reload: new fixture, same source
        Fixture f2(src);
        QVERIFY(!f2.fm.isFolded(0));   // fresh — not collapsed yet

        f2.fm.restoreCollapsedState(saved);
        QVERIFY(f2.fm.isFolded(0));
    }

    void test_foldManager_restoreCollapsedState_ignores_stale_rows()
    {
        const char* src =
            "void foo() {\n"
            "    int x;\n"
            "}\n";

        Fixture f(src);
        // Pass a row that doesn't exist in the current ranges
        f.fm.restoreCollapsedState({999});
        QVERIFY(!f.fm.isFolded(999));  // must not crash and must stay unfolded
    }

    // ── Group 7: collapse state survives re-parse ─────────────────────────────

    void test_foldManager_collapsed_state_survives_updateFoldRanges()
    {
        const char* src =
            "void foo() {\n"
            "    int x = 1;\n"
            "}\n";

        Fixture f(src);
        f.fm.toggleFold(0);
        QVERIFY(f.fm.isFolded(0));

        // Simulate the highlighter re-parsing the same source
        f.fm.updateFoldRanges(f.tree, &f.doc);

        // Fold must still be collapsed
        QVERIFY(f.fm.isFolded(0));
    }
};

QTEST_MAIN(TestFolding)
#include "test_folding.moc"
