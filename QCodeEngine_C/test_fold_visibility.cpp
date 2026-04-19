#include <QApplication>
#include <QFile>
#include <QDebug>
#include <QTimer>
#include <CodeEditor/CodeEditor.h>
#include <CodeEditor/EditorTheme.h>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    if (argc < 2) {
        qCritical() << "Usage:" << argv[0] << "<c_file>";
        return 1;
    }

    // Read test file
    QFile file(argv[1]);
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "Failed to open:" << argv[1];
        return 1;
    }
    QString source = QString::fromUtf8(file.readAll());
    file.close();

    // Create editor
    CodeEditor editor;
    editor.setTheme(QEditorTheme::oneDarkTheme());
    editor.setLineNumbersVisible(true);
    editor.setFoldingEnabled(true);
    
    // Load text
    qInfo() << "Loading file with" << source.split('\n').size() << "lines";
    editor.setText(source);
    
    // Wait a moment for signals to propagate
    QTimer::singleShot(500, [&]() {
        qInfo() << "Checking fold state after 500ms...";
        
        // Try to access fold information through the public API
        // Since we don't have direct access to FoldManager, we'll just show the editor
        editor.resize(1200, 800);
        editor.show();
        
        qInfo() << "Editor shown. Check if fold markers are visible in the gutter.";
        qInfo() << "If you see fold arrows (▶/▼) in the left gutter, folding works!";
    });

    return app.exec();
}
