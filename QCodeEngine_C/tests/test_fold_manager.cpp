// Feature: zed-style-code-folding
// Properties 3–8 and Unit Tests for FoldManager
// Validates: Requirements 3.1, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.11, 3.13, 8.1, 8.3

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QString>
#include <vector>

#include "FoldManager.h"

// ---------------------------------------------------------------------------
// Representative C source strings (same set used in FoldQuery tests)
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

    // 10. Multiple functions
    "int square(int x)\n"
    "{\n"
    "    return x * x;\n"
    "}\n"
    "\n"
    "int cube(int x)\n"
    "{\n"
    "    return x * x * x;\n"
    "}\n",
};

static constexpr int SOURCE_COUNT = static_cast<int>(sizeof(SOURCES) / sizeof(SOURCES[0]));

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestFoldManager : public QObject
{
    Q_OBJECT

private slots:

    // =======================================================================
    // Property 3: Collapsed state preserved across reparses
    // Feature: zed-style-code-folding, Property 3
    // Validates: Requirements 3.3, 8.1
    //
    // For each representative C source string:
    //   1. Parse with FoldManager.
    //   2. Collapse every other fold (deterministic "random" selection).
    //   3. Reparse with the same source.
    //   4. Assert that every range whose startRow was collapsed is still collapsed.
    // =======================================================================
    void property3_collapsedStatePreservedAcrossReparses()
    {
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            FoldManager fm;
            QString src = QString::fromUtf8(SOURCES[i]);

            // Initial parse
            fm.reparse(src);
            const std::vector<FoldRange>& ranges = fm.ranges();

            if (ranges.empty()) {
                // No folds in this source — nothing to test, skip
                continue;
            }

            // Collapse every other fold (deterministic)
            std::vector<uint32_t> collapsedRows;
            for (int j = 0; j < static_cast<int>(ranges.size()); j += 2) {
                uint32_t row = ranges[j].startRow;
                fm.toggleFold(static_cast<int>(row));
                collapsedRows.push_back(row);
            }

            // Reparse with the same source
            fm.reparse(src);

            // Assert collapsed flags are preserved for matching startRow values
            const std::vector<FoldRange>& newRanges = fm.ranges();
            for (uint32_t row : collapsedRows) {
                bool found = false;
                bool stillCollapsed = false;
                for (const FoldRange& fr : newRanges) {
                    if (fr.startRow == row) {
                        found = true;
                        stillCollapsed = fr.collapsed;
                        break;
                    }
                }
                if (found) {
                    QVERIFY2(stillCollapsed,
                             qPrintable(QString(
                                 "Source case %1: FoldRange at startRow=%2 was collapsed "
                                 "before reparse but is expanded after reparse — "
                                 "collapsed state must be preserved across reparses")
                                 .arg(i + 1)
                                 .arg(row)));
                }
            }
        }
    }

    // =======================================================================
    // Property 4: New fold ranges initialise as expanded
    // Feature: zed-style-code-folding, Property 4
    // Validates: Requirements 8.3
    //
    // For each representative C source string, construct a fresh FoldManager,
    // call reparse, and assert all collapsed == false.
    // =======================================================================
    void property4_newRangesInitialiseAsExpanded()
    {
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            FoldManager fm;
            fm.reparse(QString::fromUtf8(SOURCES[i]));

            const std::vector<FoldRange>& ranges = fm.ranges();
            for (const FoldRange& fr : ranges) {
                QVERIFY2(!fr.collapsed,
                         qPrintable(QString(
                             "Source case %1: FoldRange at startRow=%2 has collapsed=true "
                             "on a freshly constructed FoldManager — "
                             "new ranges must initialise as expanded")
                             .arg(i + 1)
                             .arg(fr.startRow)));
            }
        }
    }

    // Sanity: at least one source must produce ranges so Property 4 is non-vacuous
    void property4_sanity_atLeastOneRangeProduced()
    {
        bool anyFound = false;
        for (int i = 0; i < SOURCE_COUNT; ++i) {
            FoldManager fm;
            fm.reparse(QString::fromUtf8(SOURCES[i]));
            if (!fm.ranges().empty()) {
                anyFound = true;
                break;
            }
        }
        QVERIFY2(anyFound, "At least one source must produce fold ranges");
    }

    // =======================================================================
    // Property 5: hasFoldAt consistent with ranges()
    // Feature: zed-style-code-folding, Property 5
    // Validates: Requirements 3.5
    //
    // For each line in the source, assert:
    //   hasFoldAt(line) == (any range has startRow == line)
    // =======================================================================
    void property5_hasFoldAtConsistentWithRanges()
    {
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            FoldManager fm;
            QString src = QString::fromUtf8(SOURCES[i]);
            fm.reparse(src);

            const std::vector<FoldRange>& ranges = fm.ranges();

            // Determine the maximum line number to check
            int maxLine = src.count('\n') + 1;

            for (int line = 0; line <= maxLine; ++line) {
                // Ground truth: does any range have startRow == line?
                bool expected = false;
                for (const FoldRange& fr : ranges) {
                    if (static_cast<int>(fr.startRow) == line) {
                        expected = true;
                        break;
                    }
                }

                bool actual = fm.hasFoldAt(line);
                QVERIFY2(actual == expected,
                         qPrintable(QString(
                             "Source case %1, line %2: hasFoldAt()=%3 but "
                             "ranges() %4 a range with startRow==%2")
                             .arg(i + 1)
                             .arg(line)
                             .arg(actual ? "true" : "false")
                             .arg(expected ? "contains" : "does not contain")));
            }
        }
    }

    // =======================================================================
    // Property 6: isLineHidden consistent with collapsed ranges
    // Feature: zed-style-code-folding, Property 6
    // Validates: Requirements 3.6
    //
    // Collapse every other fold, then for each line assert:
    //   isLineHidden(line) == (any collapsed range has line > startRow && line <= endRow)
    // =======================================================================
    void property6_isLineHiddenConsistentWithCollapsedRanges()
    {
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            FoldManager fm;
            QString src = QString::fromUtf8(SOURCES[i]);
            fm.reparse(src);

            // Collapse every other fold (deterministic)
            {
                const std::vector<FoldRange>& ranges = fm.ranges();
                for (int j = 0; j < static_cast<int>(ranges.size()); j += 2) {
                    fm.toggleFold(static_cast<int>(ranges[j].startRow));
                }
            }

            const std::vector<FoldRange>& ranges = fm.ranges();
            int maxLine = src.count('\n') + 1;

            for (int line = 0; line <= maxLine; ++line) {
                // Ground truth
                bool expected = false;
                for (const FoldRange& fr : ranges) {
                    if (fr.collapsed &&
                        line > static_cast<int>(fr.startRow) &&
                        line <= static_cast<int>(fr.endRow)) {
                        expected = true;
                        break;
                    }
                }

                bool actual = fm.isLineHidden(line);
                QVERIFY2(actual == expected,
                         qPrintable(QString(
                             "Source case %1, line %2: isLineHidden()=%3 but "
                             "expected=%4 based on collapsed ranges")
                             .arg(i + 1)
                             .arg(line)
                             .arg(actual ? "true" : "false")
                             .arg(expected ? "true" : "false")));
            }
        }
    }

    // =======================================================================
    // Property 7: Toggle is an involution (double-toggle restores state)
    // Feature: zed-style-code-folding, Property 7
    // Validates: Requirements 3.7
    //
    // For each fold-start line, record collapsed state, toggle twice,
    // assert state is unchanged.
    // =======================================================================
    void property7_toggleIsAnInvolution()
    {
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            FoldManager fm;
            fm.reparse(QString::fromUtf8(SOURCES[i]));

            const std::vector<FoldRange>& ranges = fm.ranges();
            for (const FoldRange& fr : ranges) {
                int line = static_cast<int>(fr.startRow);
                bool initialState = fr.collapsed;

                // Toggle twice
                fm.toggleFold(line);
                fm.toggleFold(line);

                // Find the range again (vector may have been reallocated)
                const FoldRange* updated = fm.foldAt(line);
                QVERIFY2(updated != nullptr,
                         qPrintable(QString(
                             "Source case %1: foldAt(%2) returned null after double-toggle")
                             .arg(i + 1).arg(line)));

                QVERIFY2(updated->collapsed == initialState,
                         qPrintable(QString(
                             "Source case %1, line %2: collapsed=%3 after double-toggle "
                             "but initial state was %4 — toggle must be an involution")
                             .arg(i + 1)
                             .arg(line)
                             .arg(updated->collapsed ? "true" : "false")
                             .arg(initialState ? "true" : "false")));
            }
        }
    }

    // =======================================================================
    // Property 8: foldsChanged emitted on every reparse
    // Feature: zed-style-code-folding, Property 8
    // Validates: Requirements 3.13
    //
    // Connect QSignalSpy to foldsChanged, call reparse with non-empty source,
    // assert spy count == 1.
    // =======================================================================
    void property8_foldsChangedEmittedOnEveryReparse()
    {
        for (int i = 0; i < SOURCE_COUNT; ++i)
        {
            FoldManager fm;
            QSignalSpy spy(&fm, &FoldManager::foldsChanged);
            QVERIFY2(spy.isValid(), "QSignalSpy must be valid for foldsChanged signal");

            fm.reparse(QString::fromUtf8(SOURCES[i]));

            QVERIFY2(spy.count() == 1,
                     qPrintable(QString(
                         "Source case %1: foldsChanged emitted %2 time(s) after reparse, "
                         "expected exactly 1")
                         .arg(i + 1)
                         .arg(spy.count())));
        }
    }

    // =======================================================================
    // Unit Tests for FoldManager
    // Validates: Requirements 3.1, 3.4, 3.8, 3.11
    // =======================================================================

    // Construction: no crash, ranges() is empty
    void unit_construction_noCrashAndRangesEmpty()
    {
        FoldManager fm;
        QVERIFY2(fm.ranges().empty(),
                 "Freshly constructed FoldManager must have empty ranges()");
    }

    // reparse(""): ranges().empty(), signal emitted
    void unit_reparseEmpty_rangesEmptyAndSignalEmitted()
    {
        FoldManager fm;

        // First parse something so there are ranges
        fm.reparse(QString::fromUtf8(SOURCES[0]));
        QVERIFY(!fm.ranges().empty());

        // Now reparse with empty string
        QSignalSpy spy(&fm, &FoldManager::foldsChanged);
        QVERIFY(spy.isValid());

        fm.reparse(QString());

        QVERIFY2(fm.ranges().empty(),
                 "reparse(\"\") must clear all fold ranges");
        QVERIFY2(spy.count() == 1,
                 "reparse(\"\") must emit foldsChanged exactly once");
    }

    // toggleFold on absent line: no signal emitted
    void unit_toggleFoldAbsentLine_noSignalEmitted()
    {
        FoldManager fm;
        fm.reparse(QString::fromUtf8(SOURCES[0]));

        // Line 9999 is guaranteed to have no fold
        QSignalSpy spy(&fm, &FoldManager::foldsChanged);
        QVERIFY(spy.isValid());

        fm.toggleFold(9999);

        QVERIFY2(spy.count() == 0,
                 "toggleFold on a line with no fold must not emit foldsChanged");
    }

    // Error-node handling: no crash on source with syntax errors
    void unit_errorNodeHandling_noCrash()
    {
        FoldManager fm;

        // Deliberately malformed C — tree-sitter will produce error nodes
        const QString badSource =
            "void broken(\n"
            "{\n"
            "    int x = ;\n"
            "    return +++;\n"
            "}\n";

        // Must not crash
        fm.reparse(badSource);

        // ranges() must be accessible (may be empty or partial — no crash is the key)
        (void)fm.ranges();
        QVERIFY(true); // reaching here means no crash
    }

    // hasFoldAt on empty manager returns false
    void unit_hasFoldAt_emptyManager_returnsFalse()
    {
        FoldManager fm;
        QVERIFY2(!fm.hasFoldAt(0), "hasFoldAt on empty FoldManager must return false");
        QVERIFY2(!fm.hasFoldAt(5), "hasFoldAt on empty FoldManager must return false");
    }

    // isLineHidden on empty manager returns false
    void unit_isLineHidden_emptyManager_returnsFalse()
    {
        FoldManager fm;
        QVERIFY2(!fm.isLineHidden(0), "isLineHidden on empty FoldManager must return false");
        QVERIFY2(!fm.isLineHidden(5), "isLineHidden on empty FoldManager must return false");
    }

    // foldAt on empty manager returns nullptr
    void unit_foldAt_emptyManager_returnsNull()
    {
        FoldManager fm;
        QVERIFY2(fm.foldAt(0) == nullptr,
                 "foldAt on empty FoldManager must return nullptr");
    }
};

QTEST_MAIN(TestFoldManager)
#include "test_fold_manager.moc"
