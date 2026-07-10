#include <QtTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QFile>
#include <QTextStream>
#include "CodeEditor/CodeEditor.h"

class TestLazyLoad : public QObject
{
    Q_OBJECT

private slots:
    void testLazyLoadingProgressive();
};

void TestLazyLoad::testLazyLoadingProgressive()
{
    // Create a temporary file of size ~5 MB
    QString tempPath = "temp_5mb_test.txt";
    QFile file(tempPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    // Write 5MB of text
    QString line = "This is a dummy line for testing lazy loading. Repeating patterns to make it large.\n";
    // Each line is 84 chars/bytes. 60000 lines is ~5.04 million bytes.
    for (int i = 0; i < 60000; ++i) {
        out << line;
    }
    file.close();

    const qint64 totalBytes = file.size();

    CodeEditor editor;
    // Set editable large files preference to true so it doesn't enter the read-only large file mode sliding window
    editor.setEditableLargeFileMode(true);
    editor.show();
    QCoreApplication::processEvents();

    QSignalSpy loadedSpy(&editor, &CodeEditor::fileLoaded);

    // Load file
    QVERIFY(editor.loadFile(tempPath));

    // Initially, it should load the first chunk (2MB) and be read-only
    QVERIFY(editor.isReadOnly());
    
    // Check that we loaded a portion, not all of it
    int initialLength = editor.text().length();
    QVERIFY(initialLength > 0);
    QVERIFY(initialLength < totalBytes);

    // Wait and process events to let the background loader do its job
    int iterations = 0;
    while (editor.isReadOnly() && iterations < 100) {
        QTest::qWait(100);
        iterations++;
    }

    // Now it should be fully loaded and editable!
    QVERIFY(!editor.isReadOnly());
    QCOMPARE(editor.text().length(), totalBytes);

    // Clean up
    QFile::remove(tempPath);
}

QTEST_MAIN(TestLazyLoad)
#include "test_lazy_load.moc"
