// Feature: zed-style-code-folding, Property 2
// Property 2: Fold range fields match tree-sitter node positions
// Validates: Requirements 2.7
//
// For each known C source string containing foldable constructs, this test:
//   1. Parses the source with tree-sitter directly.
//   2. Calls FoldQuery::computeRanges to get the FoldRange list.
//   3. For each returned FoldRange, re-walks the tree to find the corresponding
//      node and verifies that startRow == ts_node_start_point(node).row and
//      endRow == ts_node_end_point(node).row.
//
// The approach avoids hard-coding expected row numbers: instead we use the
// tree-sitter API itself as the ground truth and verify that FoldQuery faithfully
// copies the node positions into FoldRange fields.

#include <QtTest/QtTest>
#include <QByteArray>
#include <QString>
#include <vector>
#include <cstring>

#include "CodeEditor/FoldQuery.h"

// tree-sitter C language declaration
extern "C" {
    const TSLanguage* tree_sitter_c(void);
}

// ---------------------------------------------------------------------------
// Fold scheme (same as used in production)
// ---------------------------------------------------------------------------

static const char* FOLD_SCHEME = R"(
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

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

// Walk the tree recursively and collect all nodes whose start/end points
// match the given FoldRange.  Returns true if at least one matching node
// is found.
static bool findMatchingNode(TSNode node,
                              uint32_t expectedStartRow,
                              uint32_t expectedEndRow,
                              uint32_t expectedStartByte,
                              uint32_t expectedEndByte)
{
    TSPoint sp = ts_node_start_point(node);
    TSPoint ep = ts_node_end_point(node);
    uint32_t sb = ts_node_start_byte(node);
    uint32_t eb = ts_node_end_byte(node);

    if (sp.row == expectedStartRow &&
        ep.row == expectedEndRow   &&
        sb     == expectedStartByte &&
        eb     == expectedEndByte)
    {
        return true;
    }

    uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_child(node, i);
        if (findMatchingNode(child,
                             expectedStartRow, expectedEndRow,
                             expectedStartByte, expectedEndByte)) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Representative C source strings
// ---------------------------------------------------------------------------

static const char* SOURCES[] = {
    // 1. Simple function body
    "int add(int a, int b)\n"
    "{\n"
    "    return a + b;\n"
    "}\n",

    // 2. Nested function with if/else
    "void process(int x)\n"
    "{\n"
    "    if (x > 0)\n"
    "    {\n"
    "        x = x * 2;\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        x = -x;\n"
    "    }\n"
    "}\n",

    // 3. Struct definition
    "struct Point\n"
    "{\n"
    "    int x;\n"
    "    int y;\n"
    "};\n",

    // 4. Union definition
    "union Data\n"
    "{\n"
    "    int   i;\n"
    "    float f;\n"
    "    char  c;\n"
    "};\n",

    // 5. Enum definition
    "enum Color\n"
    "{\n"
    "    RED,\n"
    "    GREEN,\n"
    "    BLUE\n"
    "};\n",

    // 6. Multi-line block comment
    "/* This is a\n"
    "   multi-line\n"
    "   block comment */\n"
    "int x = 0;\n",

    // 7. Preprocessor #ifdef block
    "#ifdef DEBUG\n"
    "void debug_print(const char* msg)\n"
    "{\n"
    "    puts(msg);\n"
    "}\n"
    "#endif\n",

    // 8. Preprocessor #if block
    "#if defined(PLATFORM_LINUX)\n"
    "#include <unistd.h>\n"
    "#endif\n",

    // 9. for loop body
    "void loop(void)\n"
    "{\n"
    "    int i;\n"
    "    for (i = 0; i < 10; i++)\n"
    "    {\n"
    "        int tmp = i * i;\n"
    "        (void)tmp;\n"
    "    }\n"
    "}\n",

    // 10. while loop body
    "void countdown(int n)\n"
    "{\n"
    "    while (n > 0)\n"
    "    {\n"
    "        n--;\n"
    "    }\n"
    "}\n",

    // 11. switch statement
    "void classify(int v)\n"
    "{\n"
    "    switch (v)\n"
    "    {\n"
    "        case 0:\n"
    "            break;\n"
    "        default:\n"
    "            break;\n"
    "    }\n"
    "}\n",

    // 12. Multiple functions
    "int square(int x)\n"
    "{\n"
    "    return x * x;\n"
    "}\n"
    "\n"
    "int cube(int x)\n"
    "{\n"
    "    return x * x * x;\n"
    "}\n",

    // 13. Function with block comment inside
    "/* Computes factorial\n"
    "   recursively */\n"
    "int factorial(int n)\n"
    "{\n"
    "    if (n <= 1)\n"
    "    {\n"
    "        return 1;\n"
    "    }\n"
    "    return n * factorial(n - 1);\n"
    "}\n",

    // 14. do-while loop
    "void spin(void)\n"
    "{\n"
    "    int i = 0;\n"
    "    do\n"
    "    {\n"
    "        i++;\n"
    "    } while (i < 5);\n"
    "}\n",

    // 15. Struct with nested struct
    "struct Rect\n"
    "{\n"
    "    struct Point top_left;\n"
    "    struct Point bottom_right;\n"
    "};\n",
};

static constexpr int SOURCE_COUNT = static_cast<int>(sizeof(SOURCES) / sizeof(SOURCES[0]));

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestFoldQueryProperty2 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_query = std::make_unique<FoldQuery>(tree_sitter_c(),
                                              QByteArray(FOLD_SCHEME));
        QVERIFY2(m_query->isValid(), "FoldQuery must be valid with the bundled scheme");
    }

    // Property 2: Fold range fields match tree-sitter node positions
    //
    // For every FoldRange returned by computeRanges(), there must exist a node
    // in the tree-sitter parse tree whose start/end points and start/end bytes
    // exactly match the FoldRange fields.
    //
    // This verifies Requirements 2.7:
    //   startRow  == ts_node_start_point(node).row
    //   endRow    == ts_node_end_point(node).row
    //   startByte == ts_node_start_byte(node)
    //   endByte   == ts_node_end_byte(node)
    void property2_foldRangeFieldsMatchNodePositions()
    {
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            QByteArray src(SOURCES[i]);
            TSTree* tree = parseSource(src);
            QVERIFY2(tree != nullptr,
                     qPrintable(QString("Failed to parse source case %1").arg(i + 1)));

            std::vector<FoldRange> ranges = m_query->computeRanges(tree, src);

            // For each FoldRange, verify that a tree-sitter node with exactly
            // those positions exists in the parse tree.
            for (const FoldRange& fr : ranges)
            {
                bool found = findMatchingNode(ts_tree_root_node(tree),
                                              fr.startRow,
                                              fr.endRow,
                                              fr.startByte,
                                              fr.endByte);
                QVERIFY2(found,
                         qPrintable(QString(
                             "Case %1: FoldRange {startRow=%2, endRow=%3, "
                             "startByte=%4, endByte=%5} has no matching "
                             "tree-sitter node — fields do not match any node position")
                             .arg(i + 1)
                             .arg(fr.startRow)
                             .arg(fr.endRow)
                             .arg(fr.startByte)
                             .arg(fr.endByte)));
            }

            ts_tree_delete(tree);
        }
    }

    // Sanity check: at least one source must produce at least one fold range,
    // otherwise the property test above is vacuously true.
    void sanity_atLeastOneFoldRangeProduced()
    {
        bool anyRangeFound = false;
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            QByteArray src(SOURCES[i]);
            TSTree* tree = parseSource(src);
            if (!tree) continue;

            std::vector<FoldRange> ranges = m_query->computeRanges(tree, src);
            ts_tree_delete(tree);

            if (!ranges.empty())
            {
                anyRangeFound = true;
                break;
            }
        }
        QVERIFY2(anyRangeFound,
                 "At least one source case must produce fold ranges; "
                 "otherwise Property 2 is vacuously satisfied");
    }

    // Additional check: verify specific known positions for a simple function.
    // This gives a concrete, human-readable verification that the row numbers
    // are correct (not just that some node exists with those positions).
    void property2_knownPositions_simpleFunction()
    {
        // Source:
        //   line 0: "int add(int a, int b)"
        //   line 1: "{"
        //   line 2: "    return a + b;"
        //   line 3: "}"
        //
        // The compound_statement node spans lines 1-3.
        QByteArray src =
            "int add(int a, int b)\n"
            "{\n"
            "    return a + b;\n"
            "}\n";

        TSTree* tree = parseSource(src);
        QVERIFY2(tree != nullptr, "Failed to parse simple function source");

        std::vector<FoldRange> ranges = m_query->computeRanges(tree, src);
        ts_tree_delete(tree);

        QVERIFY2(!ranges.empty(), "Expected at least one fold range for simple function");

        // Find the compound_statement range (startRow=1, endRow=3)
        bool foundCompound = false;
        for (const FoldRange& fr : ranges)
        {
            if (fr.startRow == 1 && fr.endRow == 3)
            {
                foundCompound = true;
                break;
            }
        }
        QVERIFY2(foundCompound,
                 "Expected compound_statement FoldRange with startRow=1, endRow=3 "
                 "for the simple function body");
    }

    // Verify known positions for a struct definition.
    void property2_knownPositions_structDefinition()
    {
        // Source:
        //   line 0: "struct Point"
        //   line 1: "{"
        //   line 2: "    int x;"
        //   line 3: "    int y;"
        //   line 4: "};"
        //
        // The field_declaration_list node spans lines 1-4.
        QByteArray src =
            "struct Point\n"
            "{\n"
            "    int x;\n"
            "    int y;\n"
            "};\n";

        TSTree* tree = parseSource(src);
        QVERIFY2(tree != nullptr, "Failed to parse struct source");

        std::vector<FoldRange> ranges = m_query->computeRanges(tree, src);
        ts_tree_delete(tree);

        QVERIFY2(!ranges.empty(), "Expected at least one fold range for struct definition");

        bool foundStruct = false;
        for (const FoldRange& fr : ranges)
        {
            if (fr.startRow == 1 && fr.endRow == 4)
            {
                foundStruct = true;
                break;
            }
        }
        QVERIFY2(foundStruct,
                 "Expected field_declaration_list FoldRange with startRow=1, endRow=4 "
                 "for the struct body");
    }

    // Verify known positions for a multi-line block comment.
    void property2_knownPositions_blockComment()
    {
        // Source:
        //   line 0: "/* This is a"
        //   line 1: "   multi-line"
        //   line 2: "   block comment */"
        //   line 3: "int x = 0;"
        //
        // The comment node spans lines 0-2.
        QByteArray src =
            "/* This is a\n"
            "   multi-line\n"
            "   block comment */\n"
            "int x = 0;\n";

        TSTree* tree = parseSource(src);
        QVERIFY2(tree != nullptr, "Failed to parse block comment source");

        std::vector<FoldRange> ranges = m_query->computeRanges(tree, src);
        ts_tree_delete(tree);

        QVERIFY2(!ranges.empty(), "Expected at least one fold range for block comment");

        bool foundComment = false;
        for (const FoldRange& fr : ranges)
        {
            if (fr.startRow == 0 && fr.endRow == 2)
            {
                foundComment = true;
                break;
            }
        }
        QVERIFY2(foundComment,
                 "Expected comment FoldRange with startRow=0, endRow=2 "
                 "for the multi-line block comment");
    }

private:
    std::unique_ptr<FoldQuery> m_query;
};

QTEST_MAIN(TestFoldQueryProperty2)
#include "test_fold_query_property2.moc"
