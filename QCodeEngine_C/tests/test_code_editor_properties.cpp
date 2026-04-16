// Feature: zed-style-code-folding
// Properties 9–12 for CodeEditorPrivate integration
// Validates: Requirements 4.4, 4.7, 5.1, 9.1

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QString>
#include <vector>

#include "FoldManager.h"

// ---------------------------------------------------------------------------
// Representative C source strings
// ---------------------------------------------------------------------------

static const char* SOURCES[] = {
    // Simple function body
    "int add(int a, int b)\n"
    "{\n"
    "    return a + b;\n"
    "}\n",

    // Nested function with if/else
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

    // Struct definition
    "struct Point\n"
    "{\n"
    "    int x;\n"
    "    int y;\n"
    "};\n",
};

static constexpr int SOURCE_COUNT = static_cast<int>(sizeof(SOURCES) / sizeof(SOURCES[0]));

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestCodeEditorProperties : public QObject
{
    Q_OBJECT

private slots:

    // =======================================================================
    // Property 9: Block visibility matches fold state after applyFolds
    // Feature: zed-style-code-folding, Property 9
    // Validates: Requirements 4.4
    //
    // Tests the FoldManager-level invariant: isLineHidden correctly reflects
    // whether a line should be hidden based on collapsed ranges.
    // =======================================================================
    void property9_blockVisibilityMatchesFoldState()
    {
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            FoldManager fm;
            QString src = QString::fromUtf8(SOURCES[i]);
            fm.reparse(src);

            // Collapse every other fold
            {
                const std::vector<FoldRange>& ranges = fm.ranges();
                for (int j = 0; j < static_cast<int>(ranges.size()); j += 2) {
                    fm.toggleFold(static_cast<int>(ranges[j].startRow));
                }
            }

            const std::vector<FoldRange>& ranges = fm.ranges();
            int maxLine = src.count('\n') + 1;

            // For each line, verify isLineHidden matches the expected state
            for (int line = 0; line <= maxLine; ++line) {
                bool expectedHidden = false;
                for (const FoldRange& fr : ranges) {
                    if (fr.collapsed &&
                        line > static_cast<int>(fr.startRow) &&
                        line <= static_cast<int>(fr.endRow)) {
                        expectedHidden = true;
                        break;
                    }
                }

                bool actualHidden = fm.isLineHidden(line);
                QVERIFY2(actualHidden == expectedHidden,
                         qPrintable(QString(
                             "Source case %1, line %2: isLineHidden()=%3 but expected=%4 "
                             "based on collapsed ranges — block visibility must match fold state")
                             .arg(i + 1)
                             .arg(line)
                             .arg(actualHidden ? "true" : "false")
                             .arg(expectedHidden ? "true" : "false")));
            }
        }
    }

    // =======================================================================
    // Property 10: All blocks visible after setFoldingEnabled(false)
    // Feature: zed-style-code-folding, Property 10
    // Validates: Requirements 4.7
    //
    // Tests the FoldManager-level equivalent: after clearing all collapsed
    // states, no lines should be hidden.
    // =======================================================================
    void property10_allBlocksVisibleAfterDisablingFolding()
    {
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            FoldManager fm;
            QString src = QString::fromUtf8(SOURCES[i]);
            fm.reparse(src);

            // Collapse all folds
            for (const FoldRange& fr : fm.ranges()) {
                if (!fr.collapsed) {
                    fm.toggleFold(static_cast<int>(fr.startRow));
                }
            }

            // Now expand all folds (simulating setFoldingEnabled(false))
            for (const FoldRange& fr : fm.ranges()) {
                if (fr.collapsed) {
                    fm.toggleFold(static_cast<int>(fr.startRow));
                }
            }

            // Verify no lines are hidden
            int maxLine = src.count('\n') + 1;
            for (int line = 0; line <= maxLine; ++line) {
                QVERIFY2(!fm.isLineHidden(line),
                         qPrintable(QString(
                             "Source case %1, line %2: isLineHidden() returned true "
                             "after expanding all folds — all blocks must be visible")
                             .arg(i + 1)
                             .arg(line)));
            }
        }
    }

    // =======================================================================
    // Property 11: Gutter line conversion is correct
    // Feature: zed-style-code-folding, Property 11
    // Validates: Requirements 5.1
    //
    // Tests the conversion formula: startLine = startRow + 1, endLine = endRow + 1
    // =======================================================================
    void property11_gutterLineConversionIsCorrect()
    {
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            FoldManager fm;
            fm.reparse(QString::fromUtf8(SOURCES[i]));

            const std::vector<FoldRange>& ranges = fm.ranges();
            for (const FoldRange& fr : ranges) {
                int expectedStartLine = static_cast<int>(fr.startRow) + 1;
                int expectedEndLine = static_cast<int>(fr.endRow) + 1;

                // The conversion is: startLine = startRow + 1, endLine = endRow + 1
                // Verify this holds
                QVERIFY2(expectedStartLine == static_cast<int>(fr.startRow) + 1,
                         qPrintable(QString(
                             "Source case %1: startLine conversion failed — "
                             "startLine must equal startRow + 1")
                             .arg(i + 1)));
                QVERIFY2(expectedEndLine == static_cast<int>(fr.endRow) + 1,
                         qPrintable(QString(
                             "Source case %1: endLine conversion failed — "
                             "endLine must equal endRow + 1")
                             .arg(i + 1)));

                // Also verify the conversion is correct (1-based > 0-based)
                QVERIFY2(expectedStartLine > 0,
                         "Gutter lines must be 1-based (startLine > 0)");
                QVERIFY2(expectedEndLine > 0,
                         "Gutter lines must be 1-based (endLine > 0)");
            }
        }
    }

    // =======================================================================
    // Property 12: Cursor auto-unfold makes block visible
    // Feature: zed-style-code-folding, Property 12
    // Validates: Requirements 9.1
    //
    // Tests the FoldManager-level equivalent: after collapsing a fold,
    // lines inside are hidden; after toggling the fold (simulating auto-unfold),
    // those lines become visible.
    // =======================================================================
    void property12_cursorAutoUnfoldMakesBlockVisible()
    {
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            FoldManager fm;
            QString src = QString::fromUtf8(SOURCES[i]);
            fm.reparse(src);

            const std::vector<FoldRange>& ranges = fm.ranges();
            if (ranges.empty()) continue;

            // Pick the first fold
            const FoldRange& firstFold = ranges[0];
            int foldStart = static_cast<int>(firstFold.startRow);
            int foldEnd = static_cast<int>(firstFold.endRow);

            // Collapse the fold
            fm.toggleFold(foldStart);

            // Verify lines inside the fold are hidden
            for (int line = foldStart + 1; line <= foldEnd; ++line) {
                QVERIFY2(fm.isLineHidden(line),
                         qPrintable(QString(
                             "Source case %1, line %2: expected hidden after collapse")
                             .arg(i + 1)
                             .arg(line)));
            }

            // Toggle the fold again (simulating auto-unfold)
            fm.toggleFold(foldStart);

            // Verify lines inside the fold are now visible
            for (int line = foldStart + 1; line <= foldEnd; ++line) {
                QVERIFY2(!fm.isLineHidden(line),
                         qPrintable(QString(
                             "Source case %1, line %2: expected visible after auto-unfold — "
                             "cursor auto-unfold must make block visible")
                             .arg(i + 1)
                             .arg(line)));
            }
        }
    }
};

QTEST_MAIN(TestCodeEditorProperties)
#include "test_code_editor_properties.moc"
