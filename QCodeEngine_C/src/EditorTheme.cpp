#include "CodeEditor/EditorTheme.h"
#include <QCoreApplication>
#include <QDir>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QFile>

namespace {

QString defaultEditorFontFamily()
{
    static QString resolvedFamily;
    static bool resolved = false;
    if (resolved)
        return resolvedFamily;

    resolved = true;

    QStringList candidatePaths;
    auto collectCandidates = [&candidatePaths](const QString& startPath) {
        if (startPath.isEmpty())
            return;

        QDir dir(startPath);
        for (int depth = 0; depth < 5; ++depth) {
            candidatePaths.append(dir.filePath(QStringLiteral("ZedMonoNerdFont-Regular.ttf")));
            if (!dir.cdUp())
                break;
        }
    };

    if (QCoreApplication::instance())
        collectCandidates(QCoreApplication::applicationDirPath());
    collectCandidates(QDir::currentPath());

    for (const QString& candidate : candidatePaths) {
        if (!QFile::exists(candidate))
            continue;

        const int fontId = QFontDatabase::addApplicationFont(candidate);
        if (fontId < 0)
            continue;

        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) {
            resolvedFamily = families.first();
            return resolvedFamily;
        }
    }

    resolvedFamily = QStringLiteral("JetBrains Mono");
    return resolvedFamily;
}

QString colorToJsonString(const QColor& color)
{
    if (!color.isValid())
        return QString();

    return color.alpha() == 255
        ? color.name(QColor::HexRgb)
        : color.name(QColor::HexArgb);
}

QColor colorFromJsonValue(const QJsonValue& value)
{
    if (!value.isString())
        return QColor();

    const QColor color(value.toString());
    return color.isValid() ? color : QColor();
}

void readStringField(const QJsonObject& obj, const char* key, QString& target)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    if (value.isString())
        target = value.toString();
}

void readIntField(const QJsonObject& obj, const char* key, int& target)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    if (value.isDouble())
        target = value.toInt(target);
}

void readBoolField(const QJsonObject& obj, const char* key, bool& target)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    if (value.isBool())
        target = value.toBool(target);
}

void readColorField(const QJsonObject& obj, const char* key, QColor& target)
{
    const QColor color = colorFromJsonValue(obj.value(QLatin1String(key)));
    if (color.isValid())
        target = color;
}

void readColorListField(const QJsonObject& obj, const char* key, QList<QColor>& target)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    if (!value.isArray())
        return;

    QList<QColor> colors;
    const QJsonArray array = value.toArray();
    colors.reserve(array.size());
    for (const QJsonValue& item : array) {
        const QColor color = colorFromJsonValue(item);
        if (color.isValid())
            colors.append(color);
    }

    target = colors;
}

void writeColorField(QJsonObject& obj, const char* key, const QColor& color)
{
    if (color.isValid())
        obj[QLatin1String(key)] = colorToJsonString(color);
}

void writeColorListField(QJsonObject& obj, const char* key, const QList<QColor>& colors)
{
    QJsonArray array;
    for (const QColor& color : colors) {
        if (color.isValid())
            array.append(colorToJsonString(color));
    }
    obj[QLatin1String(key)] = array;
}

QColor minimapColorWithDefaultAlpha(QColor color, int alpha)
{
    if (color.isValid() && color.alpha() == 255)
        color.setAlpha(alpha);
    return color;
}

QColor deriveMinimapBorderColor(const QEditorTheme& theme)
{
    QColor border = theme.gutterBorderColor.isValid() ? theme.gutterBorderColor : theme.foreground;
    if (!border.isValid())
        return QColor(70, 76, 84, 220);

    if (theme.minimapBackground.isValid()
        && qAbs(border.lightness() - theme.minimapBackground.lightness()) < 24) {
        border = theme.minimapBackground.lightness() < 128 ? border.lighter(165)
                                                           : border.darker(165);
    }
    return minimapColorWithDefaultAlpha(border, 220);
}

void applyDefaultMinimapTheme(QEditorTheme& theme)
{
    if (!theme.minimapBackground.isValid()) {
        theme.minimapBackground = theme.gutterBackground.isValid()
            ? theme.gutterBackground
            : theme.background;
    }

    if (!theme.minimapBorderColor.isValid())
        theme.minimapBorderColor = deriveMinimapBorderColor(theme);

    if (!theme.minimapTrackColor.isValid()) {
        QColor track = theme.gutterBorderColor.isValid() ? theme.gutterBorderColor : theme.tokenComment;
        if (!track.isValid())
            track = QColor(80, 86, 95);
        theme.minimapTrackColor = minimapColorWithDefaultAlpha(track, 95);
    }

    if (!theme.minimapViewportColor.isValid()) {
        QColor thumb = theme.selectionBackground.isValid() ? theme.selectionBackground : theme.accent;
        if (!thumb.isValid())
            thumb = QColor(110, 118, 129);
        theme.minimapViewportColor = minimapColorWithDefaultAlpha(thumb, 120);
    }

    if (!theme.minimapCaretColor.isValid())
        theme.minimapCaretColor = theme.accent.isValid() ? theme.accent : QColor(84, 174, 255);

    if (!theme.minimapErrorColor.isValid()) {
        theme.minimapErrorColor = theme.diagnosticError.isValid()
            ? theme.diagnosticError
            : QColor(224, 76, 76);
    }

    if (!theme.minimapWarningColor.isValid()) {
        theme.minimapWarningColor = theme.diagnosticWarning.isValid()
            ? theme.diagnosticWarning
            : QColor(236, 169, 62);
    }
}

} // namespace

QEditorTheme QEditorTheme::own_theme() {
    QEditorTheme t;
    t.name = "Monokai Pro Light (Filter Sun)";

    // UI Colors
    t.background = QColor("#F8EFE7");
    t.foreground = QColor("#2C232E");
    t.selectionBackground = QColor("#2672696D"); // VS Code #72696d26
    t.selectionForeground = QColor("#2C232E");
    t.currentLineBackground = QColor("#0C2C232E"); // VS Code #2c232e0c
    t.lineNumberForeground = QColor("#BEB5B3");
    t.accent = QColor("#CE4770");
    t.gutterBackground = QColor("#F8EFE7");
    t.gutterForeground = QColor("#BEB5B3");
    t.gutterBorderColor = QColor("#DED5D0");
    t.gutterActiveLineNumber = QColor("#72696D");
    t.bracketMatchBackground = QColor("#26A59C9C");
    t.bracketMatchForeground = QColor("#A59C9C");
    t.bracketMismatchBackground = QColor("#CE4770");

    // Syntax Tokens
    t.tokenKeyword         = QColor("#CE4770");        // @keyword
    t.tokenKeywordControl  = QColor("#CE4770");        // @keyword.control
    t.tokenKeywordPreproc  = QColor("#6851A2");        // @keyword.preproc / meta.preprocessor
    t.tokenType            = QColor("#2473B6");        // @type / storage.type
    t.tokenString          = QColor("#B16803");        // @string
    t.tokenNumber          = QColor("#6851A2");        // @number
    t.tokenComment         = QColor("#A59C9C");        // @comment
    t.tokenPreprocessor    = QColor("#6851A2");        // @preproc / preproc.arg
    t.tokenFunction        = QColor("#218871");        // @function
    t.tokenFunctionCall    = QColor("#218871");        // call-site functions
    t.tokenIdentifier      = QColor("#2C232E");        // @variable
    t.tokenField           = QColor("#2C232E");        // @property
    t.tokenEscape          = QColor("#6851A2");        // @string.escape
    t.tokenOperator        = QColor("#CE4770");        // @operator
    t.tokenPunctuation     = QColor("#92898A");        // @punctuation.delimiter / bracket
    t.tokenBoolean         = QColor("#6851A2");        // @boolean
    t.tokenConstantBuiltin = QColor("#2473B6");        // @constant.builtin
    t.tokenConstant        = QColor("#6851A2");        // @constant
    t.tokenAttribute       = QColor("#2473B6");        // @attribute
    t.tokenLabel           = QColor("#6851A2");        // @label

    // Search & Highlights
    t.searchHighlightBackground = QColor("#262C232E"); // VS Code #2c232e26
    t.searchHighlightForeground = QColor("#2C232E");
    t.searchCurrentMatchBackground = QColor("#33B16803");

    // Minimap & Indent Guides
    t.minimapBackground = QColor("#F8EFE7");
    t.minimapViewportColor = QColor("#2672696D"); // VS Code #72696d26
    t.indentGuideColor = QColor("#D2C9C4");

    // Rainbow Brackets
    t.rainbowColors = {
        QColor("#CE4770"),
        QColor("#D4572B"),
        QColor("#B16803"),
        QColor("#218871"),
        QColor("#2473B6"),
        QColor("#6851A2")
    };

    t.diagnosticError   = QColor("#CE4770");
    t.diagnosticWarning = QColor("#D4572B");
    t.diagnosticInfo    = QColor("#2473B6");
    t.diagnosticHint    = QColor("#6851A2");

    t.fontFamily = defaultEditorFontFamily();
    t.fontSize   = 13;

    applyDefaultMinimapTheme(t);
    return t;
}

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
    t.accent = QColor("#82D2CE");
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

    t.diagnosticError   = QColor("#E34671");   // matches bracketMismatch
    t.diagnosticWarning = QColor("#F1B467");
    t.diagnosticInfo    = QColor("#82D2CE");
    t.diagnosticHint    = QColor("#3FA266");

    t.fontFamily = defaultEditorFontFamily();
    t.fontSize   = 13;

    applyDefaultMinimapTheme(t);
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
    t.accent = QColor("#8BE9FD");
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

    t.fontFamily = defaultEditorFontFamily();
    t.fontSize   = 13;

    applyDefaultMinimapTheme(t);
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
    t.accent = QColor("#A6E22E");
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

    t.fontFamily = defaultEditorFontFamily();
    t.fontSize   = 13;

    applyDefaultMinimapTheme(t);
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
    t.accent = QColor("#61afef");
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

    t.fontFamily = defaultEditorFontFamily();
    t.fontSize   = 13;

    applyDefaultMinimapTheme(t);
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
    t.accent = QColor("#268bd2");
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

    t.fontFamily = defaultEditorFontFamily();
    t.fontSize   = 13;

    applyDefaultMinimapTheme(t);
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
    t.accent = QColor("#0366d6");
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

    t.fontFamily = defaultEditorFontFamily();
    t.fontSize   = 13;

    applyDefaultMinimapTheme(t);
    return t;
}

QEditorTheme QEditorTheme::fromJsonFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return draculaTheme();
    return fromJsonString(QString::fromUtf8(file.readAll()));
}

QEditorTheme QEditorTheme::fromJsonString(const QString& jsonStr) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return draculaTheme();

    const QJsonObject obj = doc.object();
    QEditorTheme t = draculaTheme();
    const bool hasMinimapBackground = obj.contains("minimapBackground");
    const bool hasMinimapBorderColor = obj.contains("minimapBorderColor");
    const bool hasMinimapTrackColor = obj.contains("minimapTrackColor");
    const bool hasMinimapViewportColor = obj.contains("minimapViewportColor");
    const bool hasMinimapCaretColor = obj.contains("minimapCaretColor");
    const bool hasMinimapErrorColor = obj.contains("minimapErrorColor");
    const bool hasMinimapWarningColor = obj.contains("minimapWarningColor");

    readStringField(obj, "name", t.name);

    readColorField(obj, "background", t.background);
    readColorField(obj, "foreground", t.foreground);
    readColorField(obj, "selectionBackground", t.selectionBackground);
    readColorField(obj, "selectionForeground", t.selectionForeground);
    readColorField(obj, "currentLineBackground", t.currentLineBackground);
    readColorField(obj, "lineNumberForeground", t.lineNumberForeground);
    readColorField(obj, "accent", t.accent);

    readColorField(obj, "gutterBackground", t.gutterBackground);
    readColorField(obj, "gutterForeground", t.gutterForeground);
    readColorField(obj, "gutterBorderColor", t.gutterBorderColor);
    readColorField(obj, "gutterActiveLineNumber", t.gutterActiveLineNumber);

    readColorField(obj, "bracketMatchBackground", t.bracketMatchBackground);
    readColorField(obj, "bracketMatchForeground", t.bracketMatchForeground);
    readColorField(obj, "bracketMismatchBackground", t.bracketMismatchBackground);
    readColorListField(obj, "rainbowColors", t.rainbowColors);

    readColorField(obj, "tokenKeyword", t.tokenKeyword);
    readColorField(obj, "tokenKeywordControl", t.tokenKeywordControl);
    readColorField(obj, "tokenKeywordPreproc", t.tokenKeywordPreproc);
    readColorField(obj, "tokenType", t.tokenType);
    readColorField(obj, "tokenString", t.tokenString);
    readColorField(obj, "tokenNumber", t.tokenNumber);
    readColorField(obj, "tokenComment", t.tokenComment);
    readColorField(obj, "tokenPreprocessor", t.tokenPreprocessor);
    readColorField(obj, "tokenFunction", t.tokenFunction);
    readColorField(obj, "tokenFunctionCall", t.tokenFunctionCall);
    readColorField(obj, "tokenIdentifier", t.tokenIdentifier);
    readColorField(obj, "tokenField", t.tokenField);
    readColorField(obj, "tokenEscape", t.tokenEscape);
    readColorField(obj, "tokenOperator", t.tokenOperator);
    readColorField(obj, "tokenPunctuation", t.tokenPunctuation);
    readColorField(obj, "tokenBoolean", t.tokenBoolean);
    readColorField(obj, "tokenConstantBuiltin", t.tokenConstantBuiltin);
    readColorField(obj, "tokenConstant", t.tokenConstant);
    readColorField(obj, "tokenAttribute", t.tokenAttribute);
    readColorField(obj, "tokenLabel", t.tokenLabel);

    readBoolField(obj, "keywordBold", t.keywordBold);
    readBoolField(obj, "commentItalic", t.commentItalic);
    readBoolField(obj, "functionBold", t.functionBold);
    readBoolField(obj, "typeBold", t.typeBold);

    readColorField(obj, "searchHighlightBackground", t.searchHighlightBackground);
    readColorField(obj, "searchHighlightForeground", t.searchHighlightForeground);
    readColorField(obj, "searchCurrentMatchBackground", t.searchCurrentMatchBackground);

    readColorField(obj, "minimapBackground", t.minimapBackground);
    readColorField(obj, "minimapBorderColor", t.minimapBorderColor);
    readColorField(obj, "minimapTrackColor", t.minimapTrackColor);
    readColorField(obj, "minimapViewportColor", t.minimapViewportColor);
    readColorField(obj, "minimapCaretColor", t.minimapCaretColor);
    readColorField(obj, "minimapErrorColor", t.minimapErrorColor);
    readColorField(obj, "minimapWarningColor", t.minimapWarningColor);

    readColorField(obj, "indentGuideColor", t.indentGuideColor);
    readBoolField(obj, "showIndentGuides", t.showIndentGuides);

    readStringField(obj, "fontFamily", t.fontFamily);
    readIntField(obj, "fontSize", t.fontSize);

    readColorField(obj, "diagnosticError", t.diagnosticError);
    readColorField(obj, "diagnosticWarning", t.diagnosticWarning);
    readColorField(obj, "diagnosticInfo", t.diagnosticInfo);
    readColorField(obj, "diagnosticHint", t.diagnosticHint);

    if (!hasMinimapBackground)
        t.minimapBackground = QColor();
    if (!hasMinimapBorderColor)
        t.minimapBorderColor = QColor();
    if (!hasMinimapTrackColor)
        t.minimapTrackColor = QColor();
    if (!hasMinimapViewportColor)
        t.minimapViewportColor = QColor();
    if (!hasMinimapCaretColor)
        t.minimapCaretColor = QColor();
    if (!hasMinimapErrorColor)
        t.minimapErrorColor = QColor();
    if (!hasMinimapWarningColor)
        t.minimapWarningColor = QColor();

    applyDefaultMinimapTheme(t);

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

    writeColorField(obj, "background", background);
    writeColorField(obj, "foreground", foreground);
    writeColorField(obj, "selectionBackground", selectionBackground);
    writeColorField(obj, "selectionForeground", selectionForeground);
    writeColorField(obj, "currentLineBackground", currentLineBackground);
    writeColorField(obj, "lineNumberForeground", lineNumberForeground);
    writeColorField(obj, "accent", accent);

    writeColorField(obj, "gutterBackground", gutterBackground);
    writeColorField(obj, "gutterForeground", gutterForeground);
    writeColorField(obj, "gutterBorderColor", gutterBorderColor);
    writeColorField(obj, "gutterActiveLineNumber", gutterActiveLineNumber);

    writeColorField(obj, "bracketMatchBackground", bracketMatchBackground);
    writeColorField(obj, "bracketMatchForeground", bracketMatchForeground);
    writeColorField(obj, "bracketMismatchBackground", bracketMismatchBackground);
    writeColorListField(obj, "rainbowColors", rainbowColors);

    writeColorField(obj, "tokenKeyword", tokenKeyword);
    writeColorField(obj, "tokenKeywordControl", tokenKeywordControl);
    writeColorField(obj, "tokenKeywordPreproc", tokenKeywordPreproc);
    writeColorField(obj, "tokenType", tokenType);
    writeColorField(obj, "tokenString", tokenString);
    writeColorField(obj, "tokenNumber", tokenNumber);
    writeColorField(obj, "tokenComment", tokenComment);
    writeColorField(obj, "tokenPreprocessor", tokenPreprocessor);
    writeColorField(obj, "tokenFunction", tokenFunction);
    writeColorField(obj, "tokenFunctionCall", tokenFunctionCall);
    writeColorField(obj, "tokenIdentifier", tokenIdentifier);
    writeColorField(obj, "tokenField", tokenField);
    writeColorField(obj, "tokenEscape", tokenEscape);
    writeColorField(obj, "tokenOperator", tokenOperator);
    writeColorField(obj, "tokenPunctuation", tokenPunctuation);
    writeColorField(obj, "tokenBoolean", tokenBoolean);
    writeColorField(obj, "tokenConstantBuiltin", tokenConstantBuiltin);
    writeColorField(obj, "tokenConstant", tokenConstant);
    writeColorField(obj, "tokenAttribute", tokenAttribute);
    writeColorField(obj, "tokenLabel", tokenLabel);

    obj["keywordBold"] = keywordBold;
    obj["commentItalic"] = commentItalic;
    obj["functionBold"] = functionBold;
    obj["typeBold"] = typeBold;

    writeColorField(obj, "searchHighlightBackground", searchHighlightBackground);
    writeColorField(obj, "searchHighlightForeground", searchHighlightForeground);
    writeColorField(obj, "searchCurrentMatchBackground", searchCurrentMatchBackground);

    writeColorField(obj, "minimapBackground", minimapBackground);
    writeColorField(obj, "minimapBorderColor", minimapBorderColor);
    writeColorField(obj, "minimapTrackColor", minimapTrackColor);
    writeColorField(obj, "minimapViewportColor", minimapViewportColor);
    writeColorField(obj, "minimapCaretColor", minimapCaretColor);
    writeColorField(obj, "minimapErrorColor", minimapErrorColor);
    writeColorField(obj, "minimapWarningColor", minimapWarningColor);

    writeColorField(obj, "indentGuideColor", indentGuideColor);
    obj["showIndentGuides"] = showIndentGuides;

    obj["fontFamily"] = fontFamily;
    obj["fontSize"] = fontSize;

    writeColorField(obj, "diagnosticError", diagnosticError);
    writeColorField(obj, "diagnosticWarning", diagnosticWarning);
    writeColorField(obj, "diagnosticInfo", diagnosticInfo);
    writeColorField(obj, "diagnosticHint", diagnosticHint);

    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}
