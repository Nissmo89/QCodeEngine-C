#include <QtTest>
#include <QCoreApplication>
#include <QImage>
#include <QPlainTextEdit>
#include <QStringList>

#include "CodeEditor/CodeEditor.h"
#include "CodeEditor/MiniMapWidget.h"
#include "CodeEditor/diagnosticmanager.h"

namespace {

MiniMapWidget* miniMapWidget(CodeEditor& editor)
{
    MiniMapWidget* miniMap = editor.findChild<MiniMapWidget*>();
    Q_ASSERT(miniMap);
    return miniMap;
}

QPlainTextEdit* innerEditor(CodeEditor& editor)
{
    QPlainTextEdit* inner = editor.findChild<QPlainTextEdit*>();
    Q_ASSERT(inner);
    return inner;
}

DiagnosticManager* diagnosticManager(CodeEditor& editor)
{
    DiagnosticManager* manager = editor.findChild<DiagnosticManager*>();
    Q_ASSERT(manager);
    return manager;
}

QString sampleText(int lineCount)
{
    QStringList lines;
    lines.reserve(lineCount);
    for (int i = 1; i <= lineCount; ++i)
        lines.append(QStringLiteral("line %1").arg(i));
    return lines.join('\n') + '\n';
}

bool imageContainsColor(const QImage& image, const QColor& color)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) == color)
                return true;
        }
    }
    return false;
}

} // namespace

class TestMinimapBehavior : public QObject
{
    Q_OBJECT

private slots:
    void togglesOverviewModeFromPublicApi();
    void usesCompactStickWidth();
    void rendersCaretAndDiagnosticMarkers();
};

void TestMinimapBehavior::togglesOverviewModeFromPublicApi()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.setText(sampleText(3));
    editor.show();
    QCoreApplication::processEvents();

    MiniMapWidget* miniMap = miniMapWidget(editor);
    QVERIFY2(miniMap, "MiniMap widget not found");
    QPlainTextEdit* inner = innerEditor(editor);
    QVERIFY2(inner, "Inner editor widget not found");
    QVERIFY(!miniMap->overviewVisible());
    QCOMPARE(inner->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(miniMap->minimumWidth(), 12);

    editor.setMiniMapVisible(true);
    QCoreApplication::processEvents();
    QVERIFY(miniMap->overviewVisible());
    QVERIFY(miniMap->isVisible());
    QCOMPARE(inner->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOn);
    QCOMPARE(miniMap->minimumWidth(), 14);

    editor.setMiniMapVisible(false);
    QCoreApplication::processEvents();
    QVERIFY(!miniMap->overviewVisible());
    QCOMPARE(inner->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(miniMap->minimumWidth(), 12);
}

void TestMinimapBehavior::usesCompactStickWidth()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.setText(sampleText(20));
    editor.setMiniMapVisible(true);
    editor.show();
    QCoreApplication::processEvents();

    MiniMapWidget* miniMap = miniMapWidget(editor);
    QVERIFY2(miniMap, "MiniMap widget not found");
    QVERIFY(miniMap->overviewVisible());
    QCOMPARE(miniMap->minimumWidth(), 14);
    QCOMPARE(miniMap->maximumWidth(), 14);
    QCOMPARE(miniMap->sizeHint().width(), 14);
    QCOMPARE(miniMap->width(), 14);
}

void TestMinimapBehavior::rendersCaretAndDiagnosticMarkers()
{
    CodeEditor editor;
    const QEditorTheme theme = QEditorTheme::cursorDarkTheme();
    editor.resize(900, 640);
    editor.setTheme(theme);
    editor.setText(sampleText(80));
    editor.setMiniMapVisible(true);
    editor.show();
    QCoreApplication::processEvents();

    MiniMapWidget* miniMap = miniMapWidget(editor);
    QVERIFY2(miniMap, "MiniMap widget not found");

    DiagnosticManager* manager = diagnosticManager(editor);
    QVERIFY2(manager, "Diagnostic manager not found");
    manager->setDiagnostics({
        { 6, 0, 1, QStringLiteral("error marker"), Diagnostic::Error },
        { 28, 0, 1, QStringLiteral("warning marker"), Diagnostic::Warning }
    });

    editor.goToLine(42);
    QCoreApplication::processEvents();

    const QImage image = miniMap->grab().toImage();
    QVERIFY2(imageContainsColor(image, theme.accent),
             "Caret marker color was not rendered in the compact minimap");
    QVERIFY2(imageContainsColor(image, theme.diagnosticError),
             "Error marker color was not rendered in the compact minimap");
    QVERIFY2(imageContainsColor(image, theme.diagnosticWarning),
             "Warning marker color was not rendered in the compact minimap");
}

QTEST_MAIN(TestMinimapBehavior)
#include "test_minimap_behavior.moc"
