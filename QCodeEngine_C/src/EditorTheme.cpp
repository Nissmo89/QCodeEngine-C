#include "CodeEditor/EditorTheme.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

QEditorTheme QEditorTheme::draculaTheme() {
    QEditorTheme t;
    t.name = "Dracula";
    t.background = QColor("#282A36");
    t.foreground = QColor("#F8F8F2");
    t.selectionBackground = QColor("#44475A");
    t.selectionForeground = QColor("#F8F8F2");
    t.currentLineBackground = QColor("#44475A");
    t.currentLineBackground.setAlpha(64);
    t.lineNumberForeground = QColor("#6272A4");
    t.gutterBackground = QColor("#282A36");
    t.gutterForeground = QColor("#6272A4");
    t.gutterBorderColor = QColor("#44475A");
    t.gutterActiveLineNumber = QColor("#F8F8F2"); // bright white for active line
    t.bracketMatchBackground = QColor("#FFB86C");
    t.bracketMatchBackground.setAlpha(64);
    t.bracketMatchForeground = QColor("#FFB86C");
    t.bracketMismatchBackground = QColor("#FF5555");
    t.tokenKeyword = QColor("#FF79C6");
    t.tokenType = QColor("#8BE9FD");
    t.tokenString = QColor("#F1FA8C");
    t.tokenNumber = QColor("#BD93F9");
    t.tokenComment = QColor("#6272A4");
    t.tokenPreprocessor = QColor("#FF79C6");
    t.tokenFunction = QColor("#50FA7B");
    t.tokenFunctionCall = QColor("#50FA7B");
    t.tokenIdentifier = QColor("#F8F8F2");
    t.tokenField = QColor("#8BE9FD");
    t.tokenEscape = QColor("#FF5555");
    t.tokenOperator = QColor("#FF79C6");
    t.searchHighlightBackground = QColor("#FFB86C");
    t.searchHighlightBackground.setAlpha(96);
    t.searchHighlightForeground = QColor("#282A36");
    t.searchCurrentMatchBackground = QColor("#FFB86C");
    t.minimapBackground = QColor("#21222C");
    t.minimapViewportColor = QColor("#44475A");
    t.minimapViewportColor.setAlpha(128);
    t.indentGuideColor = QColor("#44475A");

    t.rainbowColors = {
        QColor("#FF79C6"), // Pink
        QColor("#BD93F9"), // Purple
        QColor("#8BE9FD"), // Cyan
        QColor("#50FA7B"), // Green
        QColor("#FFB86C"), // Orange
        QColor("#F1FA8C")  // Yellow
    };

    t.fontFamily = "JetBrains Mono";
    t.fontSize   = 14;

    return t;
}

QEditorTheme QEditorTheme::monokaiTheme() {
    QEditorTheme t = draculaTheme(); // Fallback
    t.name = "Monokai";
    return t;
}

QEditorTheme QEditorTheme::oneDarkTheme() {
    QEditorTheme t;
    t.name = "One Dark";
    t.background = QColor("#282c34");
    t.foreground = QColor("#abb2bf");
    t.selectionBackground = QColor("#3e4451");
    t.selectionForeground = QColor("#abb2bf");
    t.currentLineBackground = QColor("#2c313c");
    t.lineNumberForeground = QColor("#4b5263");
    t.gutterBackground = QColor("#282c34");
    t.gutterForeground = QColor("#4b5263");
    t.gutterBorderColor = QColor("#282c34");
    t.gutterActiveLineNumber = QColor("#abb2bf"); // brighter text for active line
    t.bracketMatchBackground = QColor("#515a6b");
    t.bracketMatchForeground = QColor("#abb2bf");
    t.bracketMismatchBackground = QColor("#e06c75");
    
    t.tokenKeyword = QColor("#c678dd");
    t.tokenType = QColor("#e5c07b");
    t.tokenString = QColor("#98c379");
    t.tokenNumber = QColor("#d19a66");
    t.tokenComment = QColor("#5c6370");
    t.tokenPreprocessor = QColor("#c678dd");
    t.tokenFunction = QColor("#61afef");
    t.tokenFunctionCall = QColor("#61afef");
    t.tokenIdentifier = QColor("#abb2bf");
    t.tokenField = QColor("#e06c75");
    t.tokenEscape = QColor("#56b6c2");
    t.tokenOperator = QColor("#56b6c2");
    
    t.searchHighlightBackground = QColor("#3e4451");
    t.searchHighlightForeground = QColor("#abb2bf");
    t.searchCurrentMatchBackground = QColor("#314365");
    
    t.minimapBackground = QColor("#21252b");
    t.minimapViewportColor = QColor("#3e4451");
    t.minimapViewportColor.setAlpha(128);
    t.indentGuideColor = QColor("#3b4048");

    t.rainbowColors = {
        QColor("#e06c75"), // Red
        QColor("#d19a66"), // Orange
        QColor("#e5c07b"), // Yellow
        QColor("#98c379"), // Green
        QColor("#56b6c2"), // Cyan
        QColor("#c678dd")  // Purple
    };

    t.fontFamily = "JetBrains Mono";
    t.fontSize   = 14;

    return t;
}

QEditorTheme QEditorTheme::solarizedDarkTheme() {
    QEditorTheme t = draculaTheme(); // Fallback
    t.name = "Solarized Dark";
    return t;
}

QEditorTheme QEditorTheme::githubLightTheme() {
    QEditorTheme t = draculaTheme(); // Fallback
    t.name = "GitHub Light";
    return t;
}

QEditorTheme QEditorTheme::fromJsonFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return draculaTheme();
    return fromJsonString(file.readAll());
}

QEditorTheme QEditorTheme::fromJsonString(const QString& jsonStr) {
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    QJsonObject obj = doc.object();
    QEditorTheme t = draculaTheme(); // Base
    
    if (obj.contains("name")) t.name = obj["name"].toString();
    if (obj.contains("background")) t.background = QColor(obj["background"].toString());
    if (obj.contains("foreground")) t.foreground = QColor(obj["foreground"].toString());
    if (obj.contains("tokenKeyword")) t.tokenKeyword = QColor(obj["tokenKeyword"].toString());
    // (A complete implementation would parse all fields)
    
    return t;
}

void QEditorTheme::toJsonFile(const QString& path) const {
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(toJsonString().toUtf8());
    }
}

QString QEditorTheme::toJsonString() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["background"] = background.name(QColor::HexArgb);
    obj["foreground"] = foreground.name(QColor::HexArgb);
    // (A complete implementation would output all fields)
    return QJsonDocument(obj).toJson(QJsonDocument::Indented);
}
