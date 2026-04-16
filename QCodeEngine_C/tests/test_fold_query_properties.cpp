// Feature: zed-style-code-folding, Property 1
// Property 1: All fold ranges are multi-line
// Validates: Requirements 2.6
//
// RapidCheck is not available in this environment.
// The test uses a fixed set of representative C source strings that cover
// all foldable constructs (function bodies, struct/union/enum definitions,
// block comments, preprocessor conditionals).  Each input is exercised as an
// independent "case" so the test still validates the property across a
// meaningful variety of inputs.

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
// Representative C source strings
// ---------------------------------------------------------------------------

// Each entry is a multi-line C snippet that contains at least one foldable
// construct.  The property under test is: every FoldRange returned by
// computeRanges() must have startRow < endRow.

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

    // 12. Struct with nested struct
    "struct Rect\n"
    "{\n"
    "    struct Point top_left;\n"
    "    struct Point bottom_right;\n"
    "};\n",

    // 13. Multiple functions
    "int square(int x)\n"
    "{\n"
    "    return x * x;\n"
    "}\n"
    "\n"
    "int cube(int x)\n"
    "{\n"
    "    return x * x * x;\n"
    "}\n",

    // 14. Function with block comment inside
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

    // 15. do-while loop
    "void spin(void)\n"
    "{\n"
    "    int i = 0;\n"
    "    do\n"
    "    {\n"
    "        i++;\n"
    "    } while (i < 5);\n"
    "}\n",
};

static constexpr int SOURCE_COUNT = static_cast<int>(sizeof(SOURCES) / sizeof(SOURCES[0]));

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestFoldQueryProperties : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Build the FoldQuery once; reuse across all cases.
        m_query = std::make_unique<FoldQuery>(tree_sitter_c(),
                                              QByteArray(FOLD_SCHEME));
        QVERIFY2(m_query->isValid(), "FoldQuery must be valid with the bundled scheme");
    }

    // Property 1: All fold ranges are multi-line
    // For every representative C source string, every FoldRange returned by
    // computeRanges() must satisfy startRow < endRow.
    void property1_allFoldRangesAreMultiLine()
    {
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            QByteArray src(SOURCES[i]);
            TSTree* tree = parseSource(src);
            QVERIFY2(tree != nullptr,
                     qPrintable(QString("Failed to parse source case %1").arg(i + 1)));

            std::vector<FoldRange> ranges = m_query->computeRanges(tree, src);
            ts_tree_delete(tree);

            // The property: every returned range must span more than one line.
            for (const FoldRange& fr : ranges)
            {
                QVERIFY2(fr.startRow < fr.endRow,
                         qPrintable(QString(
                             "Case %1: FoldRange has startRow=%2 >= endRow=%3 "
                             "(single-line range must be filtered out)")
                             .arg(i + 1)
                             .arg(fr.startRow)
                             .arg(fr.endRow)));
            }
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
                 "otherwise Property 1 is vacuously satisfied");
    }

private:
    std::unique_ptr<FoldQuery> m_query;
};

QTEST_MAIN(TestFoldQueryProperties)
#include "test_fold_query_properties.moc"
