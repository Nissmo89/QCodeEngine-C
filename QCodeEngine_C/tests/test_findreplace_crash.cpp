#include <QtTest>
#include <QLineEdit>
#include <QPlainTextEdit>

#include "CodeEditor/CodeEditor.h"
#include "CodeEditor/FindReplaceBar.h"

namespace {

QLineEdit* lineEditByPlaceholder(FindReplaceBar* bar, const QString& placeholder)
{
    const auto edits = bar->findChildren<QLineEdit*>();
    for (QLineEdit* edit : edits) {
        if (edit && edit->placeholderText() == placeholder)
            return edit;
    }
    return nullptr;
}

} // namespace

class TestFindReplaceCrash : public QObject
{
    Q_OBJECT

private slots:
    void replaceAllThenReplaceThenEditDoesNotCrash();
    void repeatedReplaceAndManualEditDoesNotCrash();
    void replaceSingleCharWithLongerTokenDoesNotCrash();
};

void TestFindReplaceCrash::replaceAllThenReplaceThenEditDoesNotCrash()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText(
        "hello hello hello\n"
        "hello world\n"
        "hello again\n");
    QCoreApplication::processEvents();

    editor.showSearchBar();
    FindReplaceBar* bar = editor.findChild<FindReplaceBar*>();
    QVERIFY2(bar, "FindReplaceBar was not created");

    bar->openFindReplace();
    QCoreApplication::processEvents();

    QLineEdit* findEdit = lineEditByPlaceholder(bar, "Find");
    QLineEdit* replaceEdit = lineEditByPlaceholder(bar, "Replace");
    QVERIFY2(findEdit, "Could not find search input");
    QVERIFY2(replaceEdit, "Could not find replace input");

    findEdit->setText("hello");
    replaceEdit->setText("test");
    QTest::qWait(120); // Let highlight debounce complete

    QVERIFY(QMetaObject::invokeMethod(bar, "doReplaceAll", Qt::DirectConnection));
    QCoreApplication::processEvents();

    findEdit->setText("test");
    replaceEdit->setText("done");
    QTest::qWait(120);

    QVERIFY(QMetaObject::invokeMethod(bar, "findNext", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(bar, "doReplace", Qt::DirectConnection));
    QCoreApplication::processEvents();

    QPlainTextEdit* innerEditor = editor.findChild<QPlainTextEdit*>();
    QVERIFY2(innerEditor, "Inner editor widget not found");

    QTextCursor cursor = innerEditor->textCursor();
    const int donePos = innerEditor->toPlainText().indexOf("done");
    QVERIFY2(donePos >= 0, "Expected a single replaced token before manual edit");
    cursor.setPosition(donePos + 1);
    innerEditor->setTextCursor(cursor);
    QCoreApplication::processEvents();

    QTest::keyClicks(innerEditor, "X");
    QCoreApplication::processEvents();
    QTest::qWait(120);

    QVERIFY(innerEditor->toPlainText().contains("dXone"));
}

void TestFindReplaceCrash::repeatedReplaceAndManualEditDoesNotCrash()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText(
        "hello hello hello\n"
        "hello world\n"
        "hello again\n"
        "hello tail\n");
    QCoreApplication::processEvents();

    editor.showSearchBar();
    FindReplaceBar* bar = editor.findChild<FindReplaceBar*>();
    QVERIFY2(bar, "FindReplaceBar was not created");
    bar->openFindReplace();
    QCoreApplication::processEvents();

    QLineEdit* findEdit = lineEditByPlaceholder(bar, "Find");
    QLineEdit* replaceEdit = lineEditByPlaceholder(bar, "Replace");
    QVERIFY2(findEdit, "Could not find search input");
    QVERIFY2(replaceEdit, "Could not find replace input");

    QPlainTextEdit* innerEditor = editor.findChild<QPlainTextEdit*>();
    QVERIFY2(innerEditor, "Inner editor widget not found");

    for (int i = 0; i < 8; ++i) {
        findEdit->setText("hello");
        replaceEdit->setText("test");
        QTest::qWait(100);
        QVERIFY(QMetaObject::invokeMethod(bar, "doReplaceAll", Qt::DirectConnection));
        QCoreApplication::processEvents();

        findEdit->setText("test");
        replaceEdit->setText("hello");
        QTest::qWait(100);
        QVERIFY(QMetaObject::invokeMethod(bar, "findNext", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(bar, "doReplace", Qt::DirectConnection));
        QCoreApplication::processEvents();

        QTextCursor cursor = innerEditor->textCursor();
        const QString text = innerEditor->toPlainText();
        const int testPos = text.indexOf("test");
        QVERIFY2(testPos >= 0, "Expected token 'test' to remain in document");
        cursor.setPosition(testPos);
        innerEditor->setTextCursor(cursor);
        QCoreApplication::processEvents();

        QTest::keyClicks(innerEditor, "Z");
        QCoreApplication::processEvents();
        QTest::qWait(40);
    }

    QVERIFY(!innerEditor->toPlainText().isEmpty());
}

void TestFindReplaceCrash::replaceSingleCharWithLongerTokenDoesNotCrash()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();

    editor.setText(
        "x x x x\n"
        "x y x y\n"
        "tail x\n");
    QCoreApplication::processEvents();

    editor.showSearchBar();
    FindReplaceBar* bar = editor.findChild<FindReplaceBar*>();
    QVERIFY2(bar, "FindReplaceBar was not created");
    bar->openFindReplace();
    QCoreApplication::processEvents();

    QLineEdit* findEdit = lineEditByPlaceholder(bar, "Find");
    QLineEdit* replaceEdit = lineEditByPlaceholder(bar, "Replace");
    QVERIFY2(findEdit, "Could not find search input");
    QVERIFY2(replaceEdit, "Could not find replace input");

    QPlainTextEdit* innerEditor = editor.findChild<QPlainTextEdit*>();
    QVERIFY2(innerEditor, "Inner editor widget not found");

    findEdit->setText("x");
    replaceEdit->setText("xyz");
    QTest::qWait(120);
    QVERIFY(QMetaObject::invokeMethod(bar, "doReplaceAll", Qt::DirectConnection));
    QCoreApplication::processEvents();

    const QString expanded = innerEditor->toPlainText();
    QVERIFY2(expanded.contains("xyz"), "Expected expanded replacement text");

    // Force cursor movement + edit immediately after expansion to stress paint/layout.
    const int pivot = expanded.indexOf("xyz");
    QVERIFY2(pivot >= 0, "Expected to find expanded token");
    QTextCursor cursor = innerEditor->textCursor();
    cursor.setPosition(pivot + 1);
    innerEditor->setTextCursor(cursor);
    QCoreApplication::processEvents();

    QTest::keyClicks(innerEditor, "Q");
    QCoreApplication::processEvents();
    QTest::qWait(120);

    QVERIFY(innerEditor->toPlainText().contains("xQyz"));
}

QTEST_MAIN(TestFindReplaceCrash)
#include "test_findreplace_crash.moc"
