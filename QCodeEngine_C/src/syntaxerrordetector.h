#pragma once
#include <QObject>
#include <QTextDocument>
#include "CodeEditor/diagnosticmanager.h"
#include <tree_sitter/api.h>

class SyntaxErrorDetector : public QObject
{
    Q_OBJECT
public:
    explicit SyntaxErrorDetector(QObject* parent = nullptr);

    void setDocument(QTextDocument* doc);
    void setDiagnosticManager(DiagnosticManager* dm);

    // Call this after TreeSitterHighlighter emits parsed(void*)
    void analyze(void* treePtr);

    // True if last analysis found no ERROR/MISSING nodes
    bool isSyntaxClean() const { return m_syntaxClean; }

signals:
    void syntaxStateChanged(bool clean);  // TCC listens to this

private:
    void walkNode(TSNode node);
    void collectError(TSNode node);

    QTextDocument*    m_doc = nullptr;
    DiagnosticManager* m_dm = nullptr;

    QList<Diagnostic> m_pending;
    bool              m_syntaxClean = true;
};
