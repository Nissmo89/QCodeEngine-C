#include <QtTest>
#include <QPlainTextEdit>
#include <QTextBlock>

#include "CodeEditor/CodeEditor.h"

class TestBracketGuidesEditing : public QObject
{
    Q_OBJECT

private slots:
    void transientMismatchedCodeDoesNotBreakEditing();
    void unfinishedNestedLoopDoesNotBreakParentGuideRepaint();
    void guidesDoNotFoldDocumentWhenFoldingIsDisabled();
};

void TestBracketGuidesEditing::transientMismatchedCodeDoesNotBreakEditing()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.setBracketPairGuidesEnabled(true);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText(
        "int main(void) {\n"
        "    int values[3] = { 1, 2, 3 };\n"
        "    return values[(0];\n"
        "}\n");
    QCoreApplication::processEvents();

    QPlainTextEdit* innerEditor = editor.findChild<QPlainTextEdit*>();
    QVERIFY2(innerEditor, "Inner editor widget not found");

    QTextCursor cursor = innerEditor->textCursor();
    const int editPos = innerEditor->toPlainText().indexOf("values[(0");
    QVERIFY(editPos >= 0);
    cursor.setPosition(editPos + 8);
    innerEditor->setTextCursor(cursor);
    QCoreApplication::processEvents();

    const QStringList transientEdits = {
        "]",
        "{",
        ")",
        "\n    if (values[0]) {",
        "\n        return values[0];",
        "\n    }"
    };

    for (const QString& edit : transientEdits) {
        innerEditor->insertPlainText(edit);
        QCoreApplication::processEvents();
        innerEditor->viewport()->repaint();
        QCoreApplication::processEvents();
    }

    QVERIFY(!innerEditor->toPlainText().isEmpty());
}

void TestBracketGuidesEditing::unfinishedNestedLoopDoesNotBreakParentGuideRepaint()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.setBracketPairGuidesEnabled(true);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText(
        "for (int i = 0; i < 3; ++i) {\n"
        "    for (\n"
        "}\n");
    QCoreApplication::processEvents();

    QPlainTextEdit* innerEditor = editor.findChild<QPlainTextEdit*>();
    QVERIFY2(innerEditor, "Inner editor widget not found");

    innerEditor->viewport()->repaint();
    QCoreApplication::processEvents();

    QTextCursor cursor = innerEditor->textCursor();
    const int nestedFor = innerEditor->toPlainText().indexOf("for (", 1);
    QVERIFY(nestedFor >= 0);
    cursor.setPosition(nestedFor + 5);
    innerEditor->setTextCursor(cursor);
    innerEditor->insertPlainText("int j = 0; j < 3; ++j) {\n        break;\n    }");
    QCoreApplication::processEvents();
    innerEditor->viewport()->repaint();

    QVERIFY(innerEditor->toPlainText().contains("break;"));
}

void TestBracketGuidesEditing::guidesDoNotFoldDocumentWhenFoldingIsDisabled()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText(
        "void f(void) {\n"
        "    if (1) {\n"
        "        int x = 1;\n"
        "    }\n"
        "}\n");
    QCoreApplication::processEvents();

    editor.setFoldingEnabled(true);
    editor.foldLine(2);
    QCoreApplication::processEvents();

    editor.setFoldingEnabled(false);
    editor.setBracketPairGuidesEnabled(true);
    QCoreApplication::processEvents();

    QPlainTextEdit* innerEditor = editor.findChild<QPlainTextEdit*>();
    QVERIFY2(innerEditor, "Inner editor widget not found");

    QTextCursor cursor = innerEditor->textCursor();
    cursor.movePosition(QTextCursor::End);
    innerEditor->setTextCursor(cursor);
    innerEditor->insertPlainText("\nvoid g(void) {\n}\n");
    QCoreApplication::processEvents();
    innerEditor->viewport()->repaint();

    for (QTextBlock block = innerEditor->document()->begin();
         block.isValid();
         block = block.next()) {
        QVERIFY2(block.isVisible(), "Bracket guides hid a document block while folding was disabled");
    }
}

QTEST_MAIN(TestBracketGuidesEditing)
#include "test_bracket_guides_editing.moc"
