#include <QtTest>
#include <QCoreApplication>

#include "CodeEditor/CodeEditor.h"
#include "CodeEditor/MiniMapWidget.h"

namespace {

MiniMapWidget* miniMapWidget(CodeEditor& editor)
{
    MiniMapWidget* miniMap = editor.findChild<MiniMapWidget*>();
    Q_ASSERT(miniMap);
    return miniMap;
}

} // namespace

class TestMinimapBehavior : public QObject
{
    Q_OBJECT

private slots:
    void togglesVisibilityFromPublicApi();
};

void TestMinimapBehavior::togglesVisibilityFromPublicApi()
{
    CodeEditor editor;
    editor.resize(900, 640);
    editor.setText("line 1\nline 2\nline 3\n");
    editor.show();
    QCoreApplication::processEvents();

    MiniMapWidget* miniMap = miniMapWidget(editor);
    QVERIFY2(miniMap, "MiniMap widget not found");
    QVERIFY(!miniMap->isVisible());

    editor.setMiniMapVisible(true);
    QCoreApplication::processEvents();
    QVERIFY(miniMap->isVisible());

    editor.setMiniMapVisible(false);
    QCoreApplication::processEvents();
    QVERIFY(!miniMap->isVisible());
}

QTEST_MAIN(TestMinimapBehavior)
#include "test_minimap_behavior.moc"
