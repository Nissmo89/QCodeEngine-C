#include <QtTest>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QListWidget>
#include <QPlainTextEdit>

#include "AutoCompleter.h"

namespace {

QListWidget* completionPopup(QPlainTextEdit* editor)
{
    if (!editor)
        return nullptr;

    QListWidget* popup = editor->findChild<QListWidget*>();
    if (popup)
        return popup;

    return editor->viewport()->findChild<QListWidget*>();
}

void prepareEditor(QPlainTextEdit& editor)
{
    editor.resize(900, 640);
    editor.show();
    QCoreApplication::processEvents();
}

QVector<DocumentSymbol> makeFunctionSymbols()
{
    QVector<DocumentSymbol> symbols;

    DocumentSymbol compute;
    compute.name = QStringLiteral("compute_total");
    compute.completionText = compute.name;
    compute.displayText = QStringLiteral("static int compute_total(int value)");
    compute.kind = DocumentSymbolKind::Function;
    compute.isDefinition = true;
    symbols.append(compute);

    DocumentSymbol render;
    render.name = QStringLiteral("render_frame");
    render.completionText = render.name;
    render.displayText = QStringLiteral("void render_frame(void)");
    render.kind = DocumentSymbolKind::Function;
    render.isDefinition = true;
    symbols.append(render);

    return symbols;
}

} // namespace

class TestAutocompleteSymbols : public QObject
{
    Q_OBJECT

private slots:
    void popupShowsSymbolDetailButInsertsBareName();
    void symbolSuggestionsRankAboveKeywords();
};

void TestAutocompleteSymbols::popupShowsSymbolDetailButInsertsBareName()
{
    QPlainTextEdit editor;
    AutoCompleter completer;
    completer.setEditor(&editor);
    completer.setDocumentSymbols(makeFunctionSymbols());
    prepareEditor(editor);

    editor.setPlainText(QStringLiteral("comp"));
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
    editor.setFocus();
    QCoreApplication::processEvents();

    completer.updatePopup(true);

    QListWidget* popup = completionPopup(&editor);
    QVERIFY2(popup, "Completion popup not found");
    QTRY_VERIFY(popup->isVisible());
    QCOMPARE(popup->count(), 1);
    QCOMPARE(popup->item(0)->text(), QStringLiteral("static int compute_total(int value)"));

    QKeyEvent tabPress(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
    QVERIFY(completer.handleKeyPress(&tabPress));
    QCOMPARE(editor.toPlainText(), QStringLiteral("compute_total"));
    QVERIFY(!popup->isVisible());
}

void TestAutocompleteSymbols::symbolSuggestionsRankAboveKeywords()
{
    QPlainTextEdit editor;
    AutoCompleter completer;
    completer.setEditor(&editor);
    completer.setDocumentSymbols(makeFunctionSymbols());
    prepareEditor(editor);

    editor.setPlainText(QStringLiteral("re"));
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
    editor.setFocus();
    QCoreApplication::processEvents();

    completer.updatePopup(true);

    QListWidget* popup = completionPopup(&editor);
    QVERIFY2(popup, "Completion popup not found");
    QTRY_VERIFY(popup->isVisible());
    QVERIFY(popup->count() >= 1);
    QCOMPARE(popup->item(0)->text(), QStringLiteral("void render_frame(void)"));
}

QTEST_MAIN(TestAutocompleteSymbols)
#include "test_autocomplete_symbols.moc"
