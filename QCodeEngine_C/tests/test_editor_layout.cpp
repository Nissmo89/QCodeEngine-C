#include <QtTest>
#include <QCoreApplication>
#include <QFontInfo>
#include <QPlainTextEdit>
#include <QTextCursor>

#include "CodeEditor/CodeEditor.h"
#include "CodeEditor/EditorTheme.h"
#include "../src/EditorMetrics.h"

namespace {

QPlainTextEdit* innerEditor(CodeEditor& editor)
{
    QPlainTextEdit* inner = editor.findChild<QPlainTextEdit*>();
    Q_ASSERT(inner);
    return inner;
}

} // namespace

class TestEditorLayout : public QObject
{
    Q_OBJECT

private slots:
    void themedEditorUsesAirierLineHeight();
};

void TestEditorLayout::themedEditorUsesAirierLineHeight()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.setTheme(QEditorTheme::cursorDarkTheme());
    editor.show();
    QCoreApplication::processEvents();

    editor.setText("alpha\nbeta\ngamma\n");
    QCoreApplication::processEvents();

    QPlainTextEdit* inner = innerEditor(editor);
    QVERIFY2(inner, "Inner editor widget not found");
    const qreal expectedLineHeight = EditorMetrics::effectiveLineHeight(inner->font());

    QTextCursor firstLineCursor(inner->document());
    firstLineCursor.movePosition(QTextCursor::Start);
    const QRect firstRect = inner->cursorRect(firstLineCursor);

    QTextCursor secondLineCursor(inner->document());
    secondLineCursor.movePosition(QTextCursor::Start);
    secondLineCursor.movePosition(QTextCursor::Down);
    const QRect secondRect = inner->cursorRect(secondLineCursor);

    const int visualLineAdvance = secondRect.top() - firstRect.top();

    QVERIFY2(
        visualLineAdvance >= expectedLineHeight - 1.0,
        qPrintable(QString("Visual line advance %1 was too tight for expected line height %2")
                       .arg(visualLineAdvance)
                       .arg(expectedLineHeight)));

    QCOMPARE(inner->cursorWidth(), EditorMetrics::kCursorWidth);
    QCOMPARE(inner->document()->documentMargin(), qreal(EditorMetrics::kDocumentMargin));
    QCOMPARE(QFontInfo(inner->font()).pointSize(), editor.theme().fontSize);
}

QTEST_MAIN(TestEditorLayout)
#include "test_editor_layout.moc"
