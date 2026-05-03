#include <QApplication>
#include <QDebug>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <CodeEditor/CodeEditor.h>
#include <CodeEditor/EditorTheme.h>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QMainWindow mainWindow;
    mainWindow.resize(1000, 720);
    mainWindow.setWindowTitle("QCodeEditor - One Dark");

    CodeEditor* editor = new CodeEditor(&mainWindow);

    // Use One Dark theme (Zed-style)
    editor->setTheme(QEditorTheme::oneDarkTheme());

    // Configure features
    editor->setLineNumbersVisible(true);
    editor->setFoldingEnabled(true);
    editor->setAutoCompleteEnabled(true);
    editor->setTabWidth(4);
    editor->setInsertSpacesOnTab(true);

    mainWindow.setCentralWidget(editor);

    QMenuBar* menuBar = mainWindow.menuBar();
    QMenu* fileMenu = menuBar->addMenu("&File");
    QAction* openAction = fileMenu->addAction("&Open...");
    openAction->setShortcut(QKeySequence::Open);

    QObject::connect(openAction, &QAction::triggered, [&mainWindow, editor]() {
        QString fileName = QFileDialog::getOpenFileName(&mainWindow, "Open File", "", "All Files (*)");
        if (!fileName.isEmpty()) {
            if (!editor->loadFile(fileName)) {
                QMessageBox::warning(&mainWindow, "Error", "Could not open file.");
            }
        }
    });

    // Optional: observe function-jump events from Ctrl+Shift+O popup.
    QObject::connect(editor, &CodeEditor::functionSelected, [](int line) {
        qDebug() << "Jumped to line" << line;
    });

    mainWindow.show();

    return app.exec();
}
