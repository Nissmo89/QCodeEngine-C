// DiagnosticManager.h
#pragma once
#include <QObject>
#include <QTextDocument>
#include <QTextCharFormat>
#include <QColor>
#include <QList>
#include <QString>

struct Diagnostic {
    enum Severity { Error, Warning, Info, Hint };

    int      line;      // 0-based block number
    int      column;    // 0-based character offset within line
    int      length;    // number of chars to underline
    QString  message;
    Severity severity;
};

class DiagnosticManager : public QObject
{
    Q_OBJECT
public:
    explicit DiagnosticManager(QObject* parent = nullptr);

    void setDocument(QTextDocument* doc);

    // Call this with fresh diagnostics (e.g. from clang/gcc parser output)
    void setDiagnostics(const QList<Diagnostic>& diagnostics);

    void clear();

    // Colors — set these from your QEditorTheme
    void setErrorColor  (const QColor& c) { m_errorColor   = c; }
    void setWarningColor(const QColor& c) { m_warningColor = c; }
    void setInfoColor   (const QColor& c) { m_infoColor    = c; }
    void setHintColor   (const QColor& c) { m_hintColor    = c; }

    const QList<Diagnostic>& diagnostics() const { return m_diagnostics; }

signals:
    void diagnosticsChanged();

private:
    void applyToDocument();
    void clearFromDocument();
    void applyFormat(const QTextCharFormat& fmt, QTextBlock block, int start, int end);

    QTextDocument*   m_doc    = nullptr;
    QList<Diagnostic> m_diagnostics;

    QColor m_errorColor   { 220,  50,  50 };   // red
    QColor m_warningColor { 230, 160,  30 };   // orange
    QColor m_infoColor    {  50, 150, 220 };   // blue
    QColor m_hintColor    { 100, 180, 100 };   // green
};
