#include "CodeEditor/EditorTheme.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>


QEditorTheme QEditorTheme::cursorDarkTheme() {
    QEditorTheme t;
    t.name = "Cursor Dark";

    // UI Colors
    t.background = QColor("#181818");
    t.foreground = QColor("#EBE4E4E4"); // Was E4E4E4 EB
    t.selectionBackground = QColor("#99404040"); // Was 404040 99
    t.selectionForeground = QColor("#EBE4E4E4");
    t.currentLineBackground = QColor("#262626");
    t.lineNumberForeground = QColor("#42E4E4E4"); // Was E4E4E4 42
    t.gutterBackground = QColor("#181818");
    t.gutterForeground = QColor("#42E4E4E4");
    t.gutterBorderColor = QColor("#181818");
    t.gutterActiveLineNumber = QColor("#EBE4E4E4");
    t.bracketMatchBackground = QColor("#1EE4E4E4"); // Was E4E4E4 1E
    t.bracketMatchForeground = QColor("#EBE4E4E4");
    t.bracketMismatchBackground = QColor("#E34671");

    // Syntax Tokens
    t.tokenKeyword         = QColor("#82D2CE");        // @keyword        — const, struct, typedef …
    t.tokenKeywordControl  = QColor("#E34671");        // @keyword.control — if, for, return, while …
    t.tokenKeywordPreproc  = QColor("#a8cc7c");        // @keyword.preproc — #include, #define …
    t.tokenType            = QColor("#87C3FF");        // @type
    t.tokenString          = QColor("#e394dc");        // @string
    t.tokenNumber          = QColor("#ebc88d");        // @number
    t.tokenComment         = QColor("#5EE4E4E4");      // @comment
    t.tokenPreprocessor    = QColor("#a8cc7c");        // @preproc / preproc.arg
    t.tokenFunction        = QColor("#efb080");        // @function
    t.tokenFunctionCall    = QColor("#efb080");        // call-site functions
    t.tokenIdentifier      = QColor("#d6d6dd");        // @variable
    t.tokenField           = QColor("#AAA0FA");        // @property
    t.tokenEscape          = QColor("#e394dc");        // @string.escape
    t.tokenOperator        = QColor("#d6d6dd");        // @operator
    t.tokenPunctuation     = QColor("#7A7A7A");        // @punctuation.delimiter / bracket
    t.tokenBoolean         = QColor("#82D2CE");        // @boolean  — true / false
    t.tokenConstantBuiltin = QColor("#82D2CE");        // @constant.builtin — NULL
    t.tokenConstant        = QColor("#ebc88d");        // @constant — ALL_CAPS macros
    t.tokenAttribute       = QColor("#a8cc7c");        // @attribute — __attribute__, [[attr]]
    t.tokenLabel           = QColor("#E34671");        // @label     — goto labels

    // Search & Highlights
    t.searchHighlightBackground = QColor("#4488C0D0"); // Was 88C0D0 44
    t.searchHighlightForeground = QColor("#EBE4E4E4");
    t.searchCurrentMatchBackground = QColor("#6688C0D0"); // Was 88C0D0 66

    // Minimap & Indent Guides
    t.minimapBackground = QColor("#181818");
    t.minimapViewportColor = QColor("#11E4E4E4"); // Was E4E4E4 11
    t.indentGuideColor = QColor("#13E4E4E4");     // Was E4E4E4 13

    // Rainbow Brackets (Solid colors, no alpha shifting needed)
    t.rainbowColors = {
        QColor("#E34671"),
        QColor("#F1B467"),
        QColor("#ebc88d"),
        QColor("#3FA266"),
        QColor("#82D2CE"),
        QColor("#AAA0FA")
    };

    t.fontFamily = "JetBrains Mono";
    t.fontSize   = 14;

    return t;
}

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
    t.tokenKeyword         = QColor("#FF79C6");        // @keyword
    t.tokenKeywordControl  = QColor("#FF79C6");        // @keyword.control
    t.tokenKeywordPreproc  = QColor("#FF79C6");        // @keyword.preproc
    t.tokenType            = QColor("#8BE9FD");        // @type
    t.tokenString          = QColor("#F1FA8C");        // @string
    t.tokenNumber          = QColor("#BD93F9");        // @number
    t.tokenComment         = QColor("#6272A4");        // @comment
    t.tokenPreprocessor    = QColor("#FF79C6");        // @preproc
    t.tokenFunction        = QColor("#50FA7B");        // @function
    t.tokenFunctionCall    = QColor("#50FA7B");        // call sites
    t.tokenIdentifier      = QColor("#F8F8F2");        // @variable
    t.tokenField           = QColor("#8BE9FD");        // @property
    t.tokenEscape          = QColor("#FF5555");        // @string.escape
    t.tokenOperator        = QColor("#FF79C6");        // @operator
    t.tokenPunctuation     = QColor("#6272A4");        // @punctuation.*
    t.tokenBoolean         = QColor("#BD93F9");        // @boolean
    t.tokenConstantBuiltin = QColor("#BD93F9");        // @constant.builtin — NULL
    t.tokenConstant        = QColor("#BD93F9");        // @constant — ALL_CAPS
    t.tokenAttribute       = QColor("#FFB86C");        // @attribute
    t.tokenLabel           = QColor("#FF5555");        // @label
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
    
    t.tokenKeyword         = QColor("#c678dd");        // @keyword
    t.tokenKeywordControl  = QColor("#c678dd");        // @keyword.control
    t.tokenKeywordPreproc  = QColor("#c678dd");        // @keyword.preproc
    t.tokenType            = QColor("#e5c07b");        // @type
    t.tokenString          = QColor("#98c379");        // @string
    t.tokenNumber          = QColor("#d19a66");        // @number
    t.tokenComment         = QColor("#5c6370");        // @comment
    t.tokenPreprocessor    = QColor("#c678dd");        // @preproc
    t.tokenFunction        = QColor("#61afef");        // @function
    t.tokenFunctionCall    = QColor("#61afef");        // call sites
    t.tokenIdentifier      = QColor("#abb2bf");        // @variable
    t.tokenField           = QColor("#e06c75");        // @property
    t.tokenEscape          = QColor("#56b6c2");        // @string.escape
    t.tokenOperator        = QColor("#56b6c2");        // @operator
    t.tokenPunctuation     = QColor("#abb2bf");        // @punctuation.*
    t.tokenBoolean         = QColor("#d19a66");        // @boolean
    t.tokenConstantBuiltin = QColor("#e5c07b");        // @constant.builtin — NULL
    t.tokenConstant        = QColor("#d19a66");        // @constant — ALL_CAPS
    t.tokenAttribute       = QColor("#c678dd");        // @attribute
    t.tokenLabel           = QColor("#e06c75");        // @label
    
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
