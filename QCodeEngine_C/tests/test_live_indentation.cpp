#include <QtTest>
#include <QPlainTextEdit>
#include <QTextCursor>

#include "CodeEditor/CodeEditor.h"

namespace {

QPlainTextEdit* innerEditor(CodeEditor& editor)
{
    QPlainTextEdit* inner = editor.findChild<QPlainTextEdit*>();
    Q_ASSERT(inner);
    return inner;
}

void setCursorToOffset(QPlainTextEdit* editor, int offset)
{
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(offset);
    editor->setTextCursor(cursor);
}

} // namespace

class TestLiveIndentation : public QObject
{
    Q_OBJECT

private slots:
    void enterBetweenBracesSplitsCloserAndIndentsBody();
    void enterAfterControlStatementIndentsNextLine();
    void typedClosingBraceDedentsBlankLine();
    void allmanPresetReindentsOpeningBraceLine();
    void newlineUsesTabsWhenConfigured();
    void statementLineKeepsExistingIndentation();
    void smartBackspaceDropsInheritedIndentToZero();
    void smartBackspaceDropsInheritedIndentToOuterScopeColumn();
    void multiCursorEnterUsesLiveIndentation();
    void multiCursorClosingBraceDedentsEachCursor();
};

void TestLiveIndentation::enterBetweenBracesSplitsCloserAndIndentsBody()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText("int main(void) {}");
    QCoreApplication::processEvents();

    QPlainTextEdit* inner = innerEditor(editor);
    setCursorToOffset(inner, inner->toPlainText().indexOf('}'));

    QTest::keyClick(inner, Qt::Key_Return);
    QCoreApplication::processEvents();

    QCOMPARE(inner->toPlainText(), QString("int main(void) {\n    \n}"));
    QCOMPARE(inner->textCursor().blockNumber(), 1);
    QCOMPARE(inner->textCursor().positionInBlock(), 4);
}

void TestLiveIndentation::enterAfterControlStatementIndentsNextLine()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText(
        "int main(void) {\n"
        "    if (value)\n"
        "}\n");
    QCoreApplication::processEvents();

    QPlainTextEdit* inner = innerEditor(editor);
    const int ifLineOffset = inner->toPlainText().indexOf("    if (value)");
    QVERIFY(ifLineOffset >= 0);
    const int ifOffset = ifLineOffset + QString("    if (value)").size();
    setCursorToOffset(inner, ifOffset);

    QTest::keyClick(inner, Qt::Key_Return);
    QCoreApplication::processEvents();

    QCOMPARE(inner->toPlainText(),
             QString("int main(void) {\n"
                     "    if (value)\n"
                     "        \n"
                     "}\n"));
    QCOMPARE(inner->textCursor().blockNumber(), 2);
    QCOMPARE(inner->textCursor().positionInBlock(), 8);
}

void TestLiveIndentation::typedClosingBraceDedentsBlankLine()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText("int main(void) {\n    ");
    QCoreApplication::processEvents();

    QPlainTextEdit* inner = innerEditor(editor);
    setCursorToOffset(inner, inner->toPlainText().size());

    QTest::keyClicks(inner, "}");
    QCoreApplication::processEvents();

    QCOMPARE(inner->toPlainText(), QString("int main(void) {\n}"));
    QCOMPARE(inner->textCursor().blockNumber(), 1);
    QCOMPARE(inner->textCursor().positionInBlock(), 1);
}

void TestLiveIndentation::allmanPresetReindentsOpeningBraceLine()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.setIndentStylePreset(IndentStylePreset::Allman);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText(
        "int main(void) {\n"
        "    if (value)\n"
        "        ");
    QCoreApplication::processEvents();

    QPlainTextEdit* inner = innerEditor(editor);
    setCursorToOffset(inner, inner->toPlainText().size());

    QTest::keyClicks(inner, "{");
    QCoreApplication::processEvents();

    QCOMPARE(inner->toPlainText(),
             QString("int main(void) {\n"
                     "    if (value)\n"
                     "    {}"));
    QCOMPARE(inner->textCursor().blockNumber(), 2);
    QCOMPARE(inner->textCursor().positionInBlock(), 5);
}

void TestLiveIndentation::newlineUsesTabsWhenConfigured()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.setInsertSpacesOnTab(false);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText("int main(void) {}");
    QCoreApplication::processEvents();

    QPlainTextEdit* inner = innerEditor(editor);
    setCursorToOffset(inner, inner->toPlainText().indexOf('}'));

    QTest::keyClick(inner, Qt::Key_Return);
    QCoreApplication::processEvents();

    QCOMPARE(inner->toPlainText(), QString("int main(void) {\n\t\n}"));
    QCOMPARE(inner->textCursor().blockNumber(), 1);
    QCOMPARE(inner->textCursor().positionInBlock(), 1);
}

void TestLiveIndentation::statementLineKeepsExistingIndentation()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText(
        "void f(void) {\n"
        "   int a = 10;\n"
        "}\n");
    QCoreApplication::processEvents();

    QPlainTextEdit* inner = innerEditor(editor);
    const int statementOffset = inner->toPlainText().indexOf("   int a = 10;");
    QVERIFY(statementOffset >= 0);
    setCursorToOffset(inner, statementOffset + QString("   int a = 10;").size());

    QTest::keyClick(inner, Qt::Key_Return);
    QCoreApplication::processEvents();

    QCOMPARE(inner->toPlainText(),
             QString("void f(void) {\n"
                     "   int a = 10;\n"
                     "   \n"
                     "}\n"));
    QCOMPARE(inner->textCursor().blockNumber(), 2);
    QCOMPARE(inner->textCursor().positionInBlock(), 3);
}

void TestLiveIndentation::smartBackspaceDropsInheritedIndentToZero()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText(
        "void f(void) {\n"
        "   int y;\n"
        "   \n"
        "}\n");
    QCoreApplication::processEvents();

    QPlainTextEdit* inner = innerEditor(editor);
    const int blankLineOffset = inner->toPlainText().indexOf("\n   \n");
    QVERIFY(blankLineOffset >= 0);
    setCursorToOffset(inner, blankLineOffset + 4);

    QTest::keyClick(inner, Qt::Key_Backspace);
    QCoreApplication::processEvents();

    QCOMPARE(inner->toPlainText(),
             QString("void f(void) {\n"
                     "   int y;\n"
                     "\n"
                     "}\n"));
    QCOMPARE(inner->textCursor().blockNumber(), 2);
    QCOMPARE(inner->textCursor().positionInBlock(), 0);
}

void TestLiveIndentation::smartBackspaceDropsInheritedIndentToOuterScopeColumn()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText(
        "          struct pointer {\n"
        "            int y;\n"
        "            \n"
        "          }\n");
    QCoreApplication::processEvents();

    QPlainTextEdit* inner = innerEditor(editor);
    const int blankLineOffset = inner->toPlainText().indexOf("\n            \n");
    QVERIFY(blankLineOffset >= 0);
    setCursorToOffset(inner, blankLineOffset + 13);

    QTest::keyClick(inner, Qt::Key_Backspace);
    QCoreApplication::processEvents();

    QCOMPARE(inner->toPlainText(),
             QString("          struct pointer {\n"
                     "            int y;\n"
                     "          \n"
                     "          }\n"));
    QCOMPARE(inner->textCursor().blockNumber(), 2);
    QCOMPARE(inner->textCursor().positionInBlock(), 10);
}

void TestLiveIndentation::multiCursorEnterUsesLiveIndentation()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText("int a(void) {}\nint b(void) {}");
    QCoreApplication::processEvents();

    QPlainTextEdit* inner = innerEditor(editor);
    setCursorToOffset(inner, inner->toPlainText().indexOf('}'));
    QVERIFY(editor.addCursorAt(2, 14));
    QCOMPARE(editor.additionalCursorCount(), 1);

    QTest::keyClick(inner, Qt::Key_Return);
    QCoreApplication::processEvents();

    QCOMPARE(inner->toPlainText(),
             QString("int a(void) {\n"
                     "    \n"
                     "}\n"
                     "int b(void) {\n"
                     "    \n"
                     "}"));
    QCOMPARE(editor.additionalCursorCount(), 1);
    QCOMPARE(inner->textCursor().blockNumber(), 1);
    QCOMPARE(inner->textCursor().positionInBlock(), 4);
}

void TestLiveIndentation::multiCursorClosingBraceDedentsEachCursor()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText(
        "int a(void) {\n"
        "    \n"
        "int b(void) {\n"
        "    ");
    QCoreApplication::processEvents();

    QPlainTextEdit* inner = innerEditor(editor);
    const int firstClosingLine = inner->toPlainText().indexOf("\nint b(void)");
    QVERIFY(firstClosingLine >= 0);
    setCursorToOffset(inner, firstClosingLine);
    QVERIFY(editor.addCursorAt(4, 5));
    QCOMPARE(editor.additionalCursorCount(), 1);

    QTest::keyClicks(inner, "}");
    QCoreApplication::processEvents();

    QCOMPARE(inner->toPlainText(),
             QString("int a(void) {\n"
                     "}\n"
                     "int b(void) {\n"
                     "}"));
    QCOMPARE(editor.additionalCursorCount(), 1);
    QCOMPARE(inner->textCursor().blockNumber(), 1);
    QCOMPARE(inner->textCursor().positionInBlock(), 1);
}

QTEST_MAIN(TestLiveIndentation)
#include "test_live_indentation.moc"
