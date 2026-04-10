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
    QColor tokenKeyword;
    QColor tokenType;
    QColor tokenString;
    QColor tokenNumber;
    QColor tokenComment;
    QColor tokenPreprocessor;
    QColor tokenFunction;
    QColor tokenFunctionCall;
    QColor tokenIdentifier;
    QColor tokenField;
    QColor tokenEscape;         // escape sequences in strings
    QColor tokenOperator;

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
