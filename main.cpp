#include <QApplication>
#include <QDebug>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QKeySequence>
#include <QCoreApplication>
#include <QDir>
#include <CodeEditor/CodeEditor.h>
#include <CodeEditor/EditorTheme.h>

namespace {

QStringList candidateThemeDirectories()
{
    QStringList directories;

    auto collectFrom = [&directories](const QString& startPath) {
        if (startPath.isEmpty())
            return;

        QDir dir(startPath);
        for (int depth = 0; depth < 6; ++depth) {
            const QString repoThemes = dir.filePath(QStringLiteral("QCodeEngine_C/themes"));
            if (QDir(repoThemes).exists())
                directories.append(QDir(repoThemes).absolutePath());

            const QString localThemes = dir.filePath(QStringLiteral("themes"));
            if (QDir(localThemes).exists())
                directories.append(QDir(localThemes).absolutePath());

            const QString installedThemes = dir.filePath(QStringLiteral("../share/QCodeEngine_C/themes"));
            if (QDir(installedThemes).exists())
                directories.append(QDir(installedThemes).absolutePath());

            if (!dir.cdUp())
                break;
        }
    };

    collectFrom(QCoreApplication::applicationDirPath());
    collectFrom(QDir::currentPath());

    directories.removeDuplicates();
    return directories;
}

QString firstExistingThemeDirectory()
{
    const QStringList directories = candidateThemeDirectories();
    return directories.isEmpty() ? QDir::currentPath() : directories.first();
}

QString bundledThemePath(const QString& fileName)
{
    const QStringList directories = candidateThemeDirectories();
    for (const QString& directory : directories) {
        const QString path = QDir(directory).filePath(fileName);
        if (QFileInfo::exists(path))
            return path;
    }
    return QString();
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QMainWindow mainWindow;
    mainWindow.resize(1000, 720);

    CodeEditor* editor = new CodeEditor(&mainWindow);

    // Use Monokai Pro Light (Filter Sun)
    editor->setTheme(QEditorTheme::own_theme());

    const auto updateWindowTitle = [&mainWindow, editor]() {
        const QString themeName = editor->theme().name.isEmpty()
            ? QStringLiteral("Untitled Theme")
            : editor->theme().name;
        mainWindow.setWindowTitle(QStringLiteral("QCodeEditor - %1").arg(themeName));
    };
    updateWindowTitle();

    // QObject::connect(editor,&CodeEditor::)

    // Configure features
    editor->setLineNumbersVisible(true);
    editor->setMiniMapVisible(true);
    editor->setFoldingEnabled(true);
    editor->setAutoCompleteEnabled(true);
    editor->setBracketPairGuidesEnabled(true);
    editor->setTabWidth(4);
    editor->setInsertSpacesOnTab(true);
    editor->setText(
        "#include <stdio.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef struct {\n"
        "    const char *name;\n"
        "    uint32_t count;\n"
        "} Item;\n"
        "\n"
        "static void print_item(const Item *item)\n"
        "{\n"
        "    if (!item) {\n"
        "        return;\n"
        "    }\n"
        "\n"
        "    printf(\"%s: %u\\n\", item->name, item->count);\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    Item item = { \"widgets\", 42 };\n"
        "    print_item(&item);\n"
        "    return 0;\n"
        "}\n");

    mainWindow.setCentralWidget(editor);

    QMenuBar* menuBar = mainWindow.menuBar();
    QMenu* fileMenu = menuBar->addMenu("&File");
    QMenu* viewMenu = menuBar->addMenu("&View");
    QMenu* themeMenu = menuBar->addMenu("&Theme");
    QAction* openAction = fileMenu->addAction("&Open...");
    openAction->setShortcut(QKeySequence::Open);
    QAction* toggleMinimapAction = viewMenu->addAction("Toggle &Minimap");
    toggleMinimapAction->setCheckable(true);
    toggleMinimapAction->setChecked(true);
    toggleMinimapAction->setShortcut(QKeySequence(QStringLiteral("Alt+M")));
    QAction* vercelDarkAction = themeMenu->addAction("Use &Vercel Dark");
    QAction* loadThemeAction = themeMenu->addAction("&Load Theme JSON...");
    QAction* resetThemeAction = themeMenu->addAction("&Reset Demo Theme");

    QObject::connect(openAction, &QAction::triggered, [&mainWindow, editor]() {
        QString fileName = QFileDialog::getOpenFileName(&mainWindow, "Open File", "", "All Files (*)");
        if (!fileName.isEmpty()) {
            if (!editor->loadFile(fileName)) {
                QMessageBox::warning(&mainWindow, "Error", "Could not open file.");
            }
        }
    });

    QObject::connect(toggleMinimapAction, &QAction::toggled, editor, &CodeEditor::setMiniMapVisible);

    QObject::connect(vercelDarkAction, &QAction::triggered, [&mainWindow, editor, updateWindowTitle]() {
        const QString path = bundledThemePath(QStringLiteral("vercel_dark.json"));
        if (path.isEmpty()) {
            QMessageBox::warning(&mainWindow, "Theme Not Found",
                                 "Could not locate bundled theme file: vercel_dark.json");
            return;
        }

        editor->setThemeFromFile(path);
        updateWindowTitle();
    });

    QObject::connect(loadThemeAction, &QAction::triggered, [&mainWindow, editor, updateWindowTitle]() {
        const QString path = QFileDialog::getOpenFileName(
            &mainWindow,
            "Load Theme JSON",
            firstExistingThemeDirectory(),
            "JSON Theme (*.json)");
        if (path.isEmpty())
            return;

        editor->setThemeFromFile(path);
        updateWindowTitle();
    });

    QObject::connect(resetThemeAction, &QAction::triggered, [editor, updateWindowTitle]() {
        editor->setTheme(QEditorTheme::own_theme());
        updateWindowTitle();
    });

    // Optional: observe function-jump events from Ctrl+Shift+O popup.
    QObject::connect(editor, &CodeEditor::functionSelected, [](int line) {
        qDebug() << "Jumped to line" << line;
    });

    mainWindow.show();

    return app.exec();
}
