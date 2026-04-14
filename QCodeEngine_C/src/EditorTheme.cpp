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
    QEditorTheme t;
    t.name = "Monokai";
    t.background = QColor("#272822");
    t.foreground = QColor("#F8F8F2");
    t.selectionBackground = QColor("#49483E");
    t.selectionForeground = QColor("#F8F8F2");
    t.currentLineBackground = QColor("#3E3D32");
    t.lineNumberForeground = QColor("#75715E");
    t.gutterBackground = QColor("#272822");
    t.gutterForeground = QColor("#75715E");
    t.gutterBorderColor = QColor("#272822");
    t.gutterActiveLineNumber = QColor("#A6E22E");
    t.bracketMatchBackground = QColor("#49483E");
    t.bracketMatchForeground = QColor("#F8F8F2");
    t.bracketMismatchBackground = QColor("#F92672");
    
    t.tokenKeyword         = QColor("#F92672");        // @keyword
    t.tokenKeywordControl  = QColor("#F92672");        // @keyword.control
    t.tokenKeywordPreproc  = QColor("#F92672");        // @keyword.preproc
    t.tokenType            = QColor("#66D9EF");        // @type
    t.tokenString          = QColor("#E6DB74");        // @string
    t.tokenNumber          = QColor("#AE81FF");        // @number
    t.tokenComment         = QColor("#75715E");        // @comment
    t.tokenPreprocessor    = QColor("#F92672");        // @preproc
    t.tokenFunction        = QColor("#A6E22E");        // @function
    t.tokenFunctionCall    = QColor("#A6E22E");        // call sites
    t.tokenIdentifier      = QColor("#F8F8F2");        // @variable
    t.tokenField           = QColor("#FD971F");        // @property
    t.tokenEscape          = QColor("#AE81FF");        // @string.escape
    t.tokenOperator        = QColor("#F92672");        // @operator
    t.tokenPunctuation     = QColor("#F8F8F2");        // @punctuation.*
    t.tokenBoolean         = QColor("#AE81FF");        // @boolean
    t.tokenConstantBuiltin = QColor("#AE81FF");        // @constant.builtin
    t.tokenConstant        = QColor("#AE81FF");        // @constant
    t.tokenAttribute       = QColor("#A6E22E");        // @attribute
    t.tokenLabel           = QColor("#FD971F");        // @label
    
    t.searchHighlightBackground = QColor("#E6DB74");
    t.searchHighlightBackground.setAlpha(64);
    t.searchHighlightForeground = QColor("#272822");
    t.searchCurrentMatchBackground = QColor("#E6DB74");
    t.searchCurrentMatchBackground.setAlpha(128);
    
    t.minimapBackground = QColor("#1E1F1C");
    t.minimapViewportColor = QColor("#49483E");
    t.minimapViewportColor.setAlpha(128);
    t.indentGuideColor = QColor("#49483E");

    t.rainbowColors = {
        QColor("#F92672"), // Pink
        QColor("#FD971F"), // Orange
        QColor("#E6DB74"), // Yellow
        QColor("#A6E22E"), // Green
        QColor("#66D9EF"), // Blue
        QColor("#AE81FF")  // Purple
    };

    t.fontFamily = "JetBrains Mono";
    t.fontSize   = 14;

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
    QEditorTheme t;
    t.name = "Solarized Dark";
    t.background = QColor("#002b36");
    t.foreground = QColor("#839496");
    t.selectionBackground = QColor("#073642");
    t.selectionForeground = QColor("#93a1a1");
    t.currentLineBackground = QColor("#073642");
    t.lineNumberForeground = QColor("#586e75");
    t.gutterBackground = QColor("#002b36");
    t.gutterForeground = QColor("#586e75");
    t.gutterBorderColor = QColor("#002b36");
    t.gutterActiveLineNumber = QColor("#93a1a1");
    t.bracketMatchBackground = QColor("#073642");
    t.bracketMatchForeground = QColor("#93a1a1");
    t.bracketMismatchBackground = QColor("#dc322f");
    
    t.tokenKeyword         = QColor("#859900");        // @keyword
    t.tokenKeywordControl  = QColor("#859900");        // @keyword.control
    t.tokenKeywordPreproc  = QColor("#cb4b16");        // @keyword.preproc
    t.tokenType            = QColor("#b58900");        // @type
    t.tokenString          = QColor("#2aa198");        // @string
    t.tokenNumber          = QColor("#d33682");        // @number
    t.tokenComment         = QColor("#586e75");        // @comment
    t.tokenPreprocessor    = QColor("#cb4b16");        // @preproc
    t.tokenFunction        = QColor("#268bd2");        // @function
    t.tokenFunctionCall    = QColor("#268bd2");        // call sites
    t.tokenIdentifier      = QColor("#839496");        // @variable
    t.tokenField           = QColor("#268bd2");        // @property
    t.tokenEscape          = QColor("#dc322f");        // @string.escape
    t.tokenOperator        = QColor("#859900");        // @operator
    t.tokenPunctuation     = QColor("#839496");        // @punctuation.*
    t.tokenBoolean         = QColor("#d33682");        // @boolean
    t.tokenConstantBuiltin = QColor("#268bd2");        // @constant.builtin
    t.tokenConstant        = QColor("#2aa198");        // @constant
    t.tokenAttribute       = QColor("#b58900");        // @attribute
    t.tokenLabel           = QColor("#268bd2");        // @label
    
    t.searchHighlightBackground = QColor("#b58900");
    t.searchHighlightBackground.setAlpha(64);
    t.searchHighlightForeground = QColor("#002b36");
    t.searchCurrentMatchBackground = QColor("#b58900");
    t.searchCurrentMatchBackground.setAlpha(128);
    
    t.minimapBackground = QColor("#00212B");
    t.minimapViewportColor = QColor("#073642");
    t.minimapViewportColor.setAlpha(128);
    t.indentGuideColor = QColor("#073642");

    t.rainbowColors = {
        QColor("#b58900"), // Yellow
        QColor("#cb4b16"), // Orange
        QColor("#dc322f"), // Red
        QColor("#d33682"), // Magenta
        QColor("#6c71c4"), // Violet
        QColor("#268bd2")  // Blue
    };

    t.fontFamily = "JetBrains Mono";
    t.fontSize   = 14;

    return t;
}

QEditorTheme QEditorTheme::githubLightTheme() {
    QEditorTheme t;
    t.name = "GitHub Light";
    t.background = QColor("#ffffff");
    t.foreground = QColor("#24292e");
    t.selectionBackground = QColor("#c8e1ff");
    t.selectionForeground = QColor("#24292e");
    t.currentLineBackground = QColor("#f6f8fa");
    t.lineNumberForeground = QColor("#1b1f23");
    t.lineNumberForeground.setAlpha(76); // ~30% alpha
    t.gutterBackground = QColor("#ffffff");
    t.gutterForeground = QColor("#1b1f23");
    t.gutterForeground.setAlpha(76);
    t.gutterBorderColor = QColor("#eaecef");
    t.gutterActiveLineNumber = QColor("#24292e");
    t.bracketMatchBackground = QColor("#c8e1ff");
    t.bracketMatchForeground = QColor("#24292e");
    t.bracketMismatchBackground = QColor("#ffdce0");
    
    t.tokenKeyword         = QColor("#d73a49");        // @keyword
    t.tokenKeywordControl  = QColor("#d73a49");        // @keyword.control
    t.tokenKeywordPreproc  = QColor("#d73a49");        // @keyword.preproc
    t.tokenType            = QColor("#005cc5");        // @type
    t.tokenString          = QColor("#032f62");        // @string
    t.tokenNumber          = QColor("#005cc5");        // @number
    t.tokenComment         = QColor("#6a737d");        // @comment
    t.tokenPreprocessor    = QColor("#d73a49");        // @preproc
    t.tokenFunction        = QColor("#6f42c1");        // @function
    t.tokenFunctionCall    = QColor("#6f42c1");        // call sites
    t.tokenIdentifier      = QColor("#24292e");        // @variable
    t.tokenField           = QColor("#e36209");        // @property
    t.tokenEscape          = QColor("#22863a");        // @string.escape
    t.tokenOperator        = QColor("#d73a49");        // @operator
    t.tokenPunctuation     = QColor("#24292e");        // @punctuation.*
    t.tokenBoolean         = QColor("#005cc5");        // @boolean
    t.tokenConstantBuiltin = QColor("#005cc5");        // @constant.builtin
    t.tokenConstant        = QColor("#005cc5");        // @constant
    t.tokenAttribute       = QColor("#d73a49");        // @attribute
    t.tokenLabel           = QColor("#e36209");        // @label
    
    t.searchHighlightBackground = QColor("#ffdf5d");
    t.searchHighlightForeground = QColor("#24292e");
    t.searchCurrentMatchBackground = QColor("#f9c513");
    
    t.minimapBackground = QColor("#fafbfc");
    t.minimapViewportColor = QColor("#c8e1ff");
    t.minimapViewportColor.setAlpha(128);
    t.indentGuideColor = QColor("#eaecef");

    t.rainbowColors = {
        QColor("#d73a49"), // Red
        QColor("#e36209"), // Orange
        QColor("#dbab09"), // Yellow
        QColor("#28a745"), // Green
        QColor("#005cc5"), // Blue
        QColor("#6f42c1")  // Purple
    };

    t.fontFamily = "JetBrains Mono";
    t.fontSize   = 14;

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
