#pragma once
#include <QString>
#include <QColor>

struct QEditorTheme {
    QString name;

    // Editor area
    QColor background;
    QColor foreground;           // default text color
    QColor selectionBackground;
    QColor selectionForeground;
    QColor currentLineBackground;
    QColor lineNumberForeground;

    // Gutter
    QColor gutterBackground;
    QColor gutterForeground;
    QColor gutterBorderColor;
    QColor gutterActiveLineNumber; // line number color for the current (active) line

    // Brackets
    QColor bracketMatchBackground;
    QColor bracketMatchForeground;
    QColor bracketMismatchBackground;
    QList<QColor> rainbowColors;

    // Syntax token colors
    QColor tokenKeyword;          // @keyword        — storage / type qualifiers
    QColor tokenKeywordControl;   // @keyword.control — control flow (if/for/while/…)
    QColor tokenKeywordPreproc;   // @keyword.preproc — #define / #include / …
    QColor tokenType;             // @type           — type identifiers / primitives
    QColor tokenString;           // @string         — string & char literals
    QColor tokenNumber;           // @number         — numeric literals
    QColor tokenComment;          // @comment
    QColor tokenPreprocessor;     // @preproc / preproc.arg — preprocessor fragments
    QColor tokenFunction;         // @function       — function declarations
    QColor tokenFunctionCall;     // (reused for call sites)
    QColor tokenIdentifier;       // @variable       — plain identifiers
    QColor tokenField;            // @property       — struct field identifiers
    QColor tokenEscape;           // @string.escape  — escape sequences inside strings
    QColor tokenOperator;         // @operator       — binary / unary operators
    QColor tokenPunctuation;      // @punctuation.delimiter / @punctuation.bracket
    QColor tokenBoolean;          // @boolean        — true / false
    QColor tokenConstantBuiltin;  // @constant.builtin — NULL / nullptr
    QColor tokenConstant;         // @constant       — ALL_CAPS identifiers
    QColor tokenAttribute;        // @attribute      — GNU __attribute__ / C23 [[attr]]
    QColor tokenLabel;            // @label          — goto labels

    // Token decoration flags per token (optional bold/italic)
    bool keywordBold      = true;
    bool commentItalic    = true;
    bool functionBold     = false;
    bool typeBold         = false;

    // Search
    QColor searchHighlightBackground;
    QColor searchHighlightForeground;
    QColor searchCurrentMatchBackground;

    // Minimap
    QColor minimapBackground;
    QColor minimapViewportColor;  // the visible-area box

    // Indentation guides
    QColor indentGuideColor;
    bool   showIndentGuides = true;

    // Font
    QString fontFamily = "Monospace";
    int     fontSize   = 13;

    // Static constructors
    static QEditorTheme cursorDarkTheme();
    static QEditorTheme draculaTheme();
    static QEditorTheme monokaiTheme();
    static QEditorTheme oneDarkTheme();
    static QEditorTheme solarizedDarkTheme();
    static QEditorTheme githubLightTheme();
    static QEditorTheme fromJsonFile(const QString& path);
    static QEditorTheme fromJsonString(const QString& json);

    void toJsonFile(const QString& path) const;
    QString toJsonString() const;
};
