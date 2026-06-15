#include <QtTest>
#include <QCoreApplication>
#include <QListWidget>
#include <QPlainTextEdit>

#include "CodeEditor/CodeEditor.h"

namespace {

QPlainTextEdit* innerEditor(CodeEditor& editor)
{
    return editor.findChild<QPlainTextEdit*>();
}

QListWidget* completionPopup(QPlainTextEdit* editor)
{
    if (!editor)
        return nullptr;

    QListWidget* popup = editor->findChild<QListWidget*>();
    if (popup)
        return popup;

    return editor->viewport()->findChild<QListWidget*>();
}

void prepareEditor(CodeEditor& editor)
{
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();
}

} // namespace

class TestAutocompleteBehavior : public QObject
{
    Q_OBJECT

private slots:
    void enterKeepsEditingInsteadOfAcceptingSuggestion();
    void caretNavigationDismissesPopup();
    void backspaceReevaluatesPopupVisibility();
    void treeSitterSymbolsDriveSuggestions();
};

void TestAutocompleteBehavior::enterKeepsEditingInsteadOfAcceptingSuggestion()
{
    CodeEditor editor;
    prepareEditor(editor);
    QPlainTextEdit* inner = innerEditor(editor);
    QVERIFY2(inner, "Inner editor widget not found");
    QListWidget* popup = completionPopup(inner);
    QVERIFY2(popup, "Completion popup not found");

    inner->setFocus();
    QCoreApplication::processEvents();

    QTest::keyClicks(inner, "re");
    QTRY_VERIFY(popup->isVisible());

    QTest::keyClick(inner, Qt::Key_Return);
    QCoreApplication::processEvents();

    QVERIFY(!popup->isVisible());
    QCOMPARE(inner->toPlainText(), QString("re\n"));
}

void TestAutocompleteBehavior::caretNavigationDismissesPopup()
{
    CodeEditor editor;
    prepareEditor(editor);
    QPlainTextEdit* inner = innerEditor(editor);
    QVERIFY2(inner, "Inner editor widget not found");
    QListWidget* popup = completionPopup(inner);
    QVERIFY2(popup, "Completion popup not found");

    inner->setFocus();
    QCoreApplication::processEvents();

    QTest::keyClicks(inner, "ret");
    QTRY_VERIFY(popup->isVisible());
    QCOMPARE(inner->textCursor().position(), 3);

    QTest::keyClick(inner, Qt::Key_Left);
    QCoreApplication::processEvents();

    QVERIFY(!popup->isVisible());
    QCOMPARE(inner->textCursor().position(), 2);
    QCOMPARE(inner->toPlainText(), QString("ret"));
}

void TestAutocompleteBehavior::backspaceReevaluatesPopupVisibility()
{
    CodeEditor editor;
    prepareEditor(editor);
    QPlainTextEdit* inner = innerEditor(editor);
    QVERIFY2(inner, "Inner editor widget not found");
    QListWidget* popup = completionPopup(inner);
    QVERIFY2(popup, "Completion popup not found");

    inner->setFocus();
    QCoreApplication::processEvents();

    QTest::keyClicks(inner, "ret");
    QTRY_VERIFY(popup->isVisible());

    QTest::keyClick(inner, Qt::Key_Backspace);
    QTRY_VERIFY(popup->isVisible());
    QCOMPARE(inner->toPlainText(), QString("re"));

    QTest::keyClick(inner, Qt::Key_Backspace);
    QCoreApplication::processEvents();

    QVERIFY(!popup->isVisible());
    QCOMPARE(inner->toPlainText(), QString("r"));
}

void TestAutocompleteBehavior::treeSitterSymbolsDriveSuggestions()
{
    CodeEditor editor;
    prepareEditor(editor);
    QPlainTextEdit* inner = innerEditor(editor);
    QVERIFY2(inner, "Inner editor widget not found");
    QListWidget* popup = completionPopup(inner);
    QVERIFY2(popup, "Completion popup not found");

    editor.setText(
        "static int compute_total(int value)\n"
        "{\n"
        "    return value + 1;\n"
        "}\n\n");
    QCoreApplication::processEvents();

    inner->setFocus();
    QCoreApplication::processEvents();

    QTest::keyClicks(inner, "comp");
    QTRY_VERIFY(popup->isVisible());
    QVERIFY(popup->count() >= 1);
    QCOMPARE(popup->item(0)->text(), QString("static int compute_total(int value)"));

    QTest::keyClick(inner, Qt::Key_Tab);
    QCoreApplication::processEvents();

    QVERIFY(!popup->isVisible());
    QVERIFY(inner->toPlainText().endsWith(QStringLiteral("compute_total")));
}

QTEST_MAIN(TestAutocompleteBehavior)
#include "test_autocomplete_behavior.moc"
