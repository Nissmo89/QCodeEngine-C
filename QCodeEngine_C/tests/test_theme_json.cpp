#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTemporaryDir>

#include "CodeEditor/CodeEditor.h"
#include "CodeEditor/EditorTheme.h"

namespace {

class ScopedCurrentDir
{
public:
    explicit ScopedCurrentDir(const QString& nextDir)
        : m_previous(QDir::currentPath())
        , m_active(QDir::setCurrent(nextDir))
    {
    }

    ~ScopedCurrentDir()
    {
        if (m_active)
            QDir::setCurrent(m_previous);
    }

    bool isActive() const { return m_active; }

private:
    QString m_previous;
    bool m_active = false;
};

void compareThemes(const QEditorTheme& actual, const QEditorTheme& expected)
{
    QCOMPARE(actual.name, expected.name);
    QCOMPARE(actual.background, expected.background);
    QCOMPARE(actual.foreground, expected.foreground);
    QCOMPARE(actual.selectionBackground, expected.selectionBackground);
    QCOMPARE(actual.selectionForeground, expected.selectionForeground);
    QCOMPARE(actual.currentLineBackground, expected.currentLineBackground);
    QCOMPARE(actual.lineNumberForeground, expected.lineNumberForeground);
    QCOMPARE(actual.accent, expected.accent);

    QCOMPARE(actual.gutterBackground, expected.gutterBackground);
    QCOMPARE(actual.gutterForeground, expected.gutterForeground);
    QCOMPARE(actual.gutterBorderColor, expected.gutterBorderColor);
    QCOMPARE(actual.gutterActiveLineNumber, expected.gutterActiveLineNumber);

    QCOMPARE(actual.bracketMatchBackground, expected.bracketMatchBackground);
    QCOMPARE(actual.bracketMatchForeground, expected.bracketMatchForeground);
    QCOMPARE(actual.bracketMismatchBackground, expected.bracketMismatchBackground);
    QCOMPARE(actual.rainbowColors.size(), expected.rainbowColors.size());
    for (int i = 0; i < actual.rainbowColors.size(); ++i)
        QCOMPARE(actual.rainbowColors.at(i), expected.rainbowColors.at(i));

    QCOMPARE(actual.tokenKeyword, expected.tokenKeyword);
    QCOMPARE(actual.tokenKeywordControl, expected.tokenKeywordControl);
    QCOMPARE(actual.tokenKeywordPreproc, expected.tokenKeywordPreproc);
    QCOMPARE(actual.tokenType, expected.tokenType);
    QCOMPARE(actual.tokenString, expected.tokenString);
    QCOMPARE(actual.tokenNumber, expected.tokenNumber);
    QCOMPARE(actual.tokenComment, expected.tokenComment);
    QCOMPARE(actual.tokenPreprocessor, expected.tokenPreprocessor);
    QCOMPARE(actual.tokenFunction, expected.tokenFunction);
    QCOMPARE(actual.tokenFunctionCall, expected.tokenFunctionCall);
    QCOMPARE(actual.tokenIdentifier, expected.tokenIdentifier);
    QCOMPARE(actual.tokenField, expected.tokenField);
    QCOMPARE(actual.tokenEscape, expected.tokenEscape);
    QCOMPARE(actual.tokenOperator, expected.tokenOperator);
    QCOMPARE(actual.tokenPunctuation, expected.tokenPunctuation);
    QCOMPARE(actual.tokenBoolean, expected.tokenBoolean);
    QCOMPARE(actual.tokenConstantBuiltin, expected.tokenConstantBuiltin);
    QCOMPARE(actual.tokenConstant, expected.tokenConstant);
    QCOMPARE(actual.tokenAttribute, expected.tokenAttribute);
    QCOMPARE(actual.tokenLabel, expected.tokenLabel);

    QCOMPARE(actual.keywordBold, expected.keywordBold);
    QCOMPARE(actual.commentItalic, expected.commentItalic);
    QCOMPARE(actual.functionBold, expected.functionBold);
    QCOMPARE(actual.typeBold, expected.typeBold);

    QCOMPARE(actual.searchHighlightBackground, expected.searchHighlightBackground);
    QCOMPARE(actual.searchHighlightForeground, expected.searchHighlightForeground);
    QCOMPARE(actual.searchCurrentMatchBackground, expected.searchCurrentMatchBackground);

    QCOMPARE(actual.minimapBackground, expected.minimapBackground);
    QCOMPARE(actual.minimapBorderColor, expected.minimapBorderColor);
    QCOMPARE(actual.minimapTrackColor, expected.minimapTrackColor);
    QCOMPARE(actual.minimapViewportColor, expected.minimapViewportColor);
    QCOMPARE(actual.minimapCaretColor, expected.minimapCaretColor);
    QCOMPARE(actual.minimapErrorColor, expected.minimapErrorColor);
    QCOMPARE(actual.minimapWarningColor, expected.minimapWarningColor);
    QCOMPARE(actual.indentGuideColor, expected.indentGuideColor);
    QCOMPARE(actual.showIndentGuides, expected.showIndentGuides);

    QCOMPARE(actual.fontFamily, expected.fontFamily);
    QCOMPARE(actual.fontSize, expected.fontSize);

    QCOMPARE(actual.diagnosticError, expected.diagnosticError);
    QCOMPARE(actual.diagnosticWarning, expected.diagnosticWarning);
    QCOMPARE(actual.diagnosticInfo, expected.diagnosticInfo);
    QCOMPARE(actual.diagnosticHint, expected.diagnosticHint);
}

} // namespace

class TestThemeJson : public QObject
{
    Q_OBJECT

private slots:
    void jsonRoundTripPreservesThemeFields();
    void ctrlTCycleIncludesJsonThemes();
};

void TestThemeJson::jsonRoundTripPreservesThemeFields()
{
    QEditorTheme original = QEditorTheme::cursorDarkTheme();
    original.name = "Round Trip Theme";
    original.selectionBackground = QColor("#33445566");
    original.selectionForeground = QColor("#fefefe");
    original.currentLineBackground = QColor("#11223344");
    original.gutterBorderColor = QColor("#223344");
    original.bracketMatchBackground = QColor("#55667788");
    original.bracketMismatchBackground = QColor("#ff3366");
    original.rainbowColors = {
        QColor("#112233"),
        QColor("#445566"),
        QColor("#778899")
    };
    original.keywordBold = false;
    original.commentItalic = false;
    original.functionBold = true;
    original.typeBold = true;
    original.searchCurrentMatchBackground = QColor("#66112233");
    original.minimapBorderColor = QColor("#556677");
    original.minimapTrackColor = QColor("#22334488");
    original.minimapViewportColor = QColor("#22446688");
    original.minimapCaretColor = QColor("#88bbff");
    original.minimapErrorColor = QColor("#ff4455");
    original.minimapWarningColor = QColor("#ffaa33");
    original.indentGuideColor = QColor("#1d1d1d");
    original.showIndentGuides = false;
    original.fontFamily = "Fira Code";
    original.fontSize = 17;
    original.diagnosticHint = QColor("#123abc");

    const QEditorTheme loaded = QEditorTheme::fromJsonString(original.toJsonString());
    compareThemes(loaded, original);
}

void TestThemeJson::ctrlTCycleIncludesJsonThemes()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for JSON theme test");

    const QString themesDir = tempDir.filePath("QCodeEngine_C/themes");
    QVERIFY2(QDir().mkpath(themesDir), "Failed to create temporary themes directory");

    QEditorTheme jsonTheme = QEditorTheme::oneDarkTheme();
    jsonTheme.name = "Cycle Test Theme";
    jsonTheme.background = QColor("#101112");
    jsonTheme.foreground = QColor("#f4f4f5");
    jsonTheme.accent = QColor("#0091ff");
    jsonTheme.toJsonFile(themesDir + "/cycle_test.json");

    ScopedCurrentDir scopedDir(tempDir.path());
    QVERIFY2(scopedDir.isActive(), "Failed to switch current directory for theme discovery");

    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    QPlainTextEdit* innerEditor = editor.findChild<QPlainTextEdit*>();
    QVERIFY2(innerEditor, "Inner editor widget not found");
    innerEditor->setFocus();
    QCoreApplication::processEvents();

    bool foundJsonTheme = false;
    QStringList seenThemeNames;
    for (int i = 0; i < 24; ++i) {
        QTest::keyClick(innerEditor, Qt::Key_T, Qt::ControlModifier);
        QCoreApplication::processEvents();

        const QEditorTheme currentTheme = editor.theme();
        seenThemeNames.append(currentTheme.name);
        if (currentTheme.name == jsonTheme.name) {
            QCOMPARE(currentTheme.background, jsonTheme.background);
            QCOMPARE(currentTheme.foreground, jsonTheme.foreground);
            QCOMPARE(currentTheme.accent, jsonTheme.accent);
            foundJsonTheme = true;
            break;
        }
    }

    QVERIFY2(
        foundJsonTheme,
        qPrintable(QString("Ctrl+T did not reach a JSON theme. Seen: %1").arg(seenThemeNames.join(", "))));
}

QTEST_MAIN(TestThemeJson)
#include "test_theme_json.moc"
