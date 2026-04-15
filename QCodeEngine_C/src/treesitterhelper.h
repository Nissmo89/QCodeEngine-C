#ifndef TREESITTERHELPER_H
#define TREESITTERHELPER_H

#include <QString>
#include <QVector>
#include <QByteArray>
#include <QPlainTextEdit>
#include <tree_sitter/api.h>

// Declare the external C grammar function
extern "C" const TSLanguage *tree_sitter_c();

struct FunctionRange {
    int startLine;
    int startCol;
    int endLine;
    int endCol;
    QString signature;   // optional, for breadcrumb text
    TSNode node;         // optional
};

class TreeSitterHelper
{
public:
    // Constructor takes the source code
    explicit TreeSitterHelper(const QString &sourceCode);
    ~TreeSitterHelper();

    // The main helper function
    QVector<TSNode> getNodesByType(const QString &nodeType);

    // Helper to extract the actual code string from a node
    QString getNodeText(TSNode node) const;

    QString extractFunctionSignature(TSNode func) const;

    // ✅ REMOVED QsciScintilla methods - now work with QPlainTextEdit
    // int getCursorByteOffset(QsciScintilla* editor);
    // TSNode getNodeAtCursor(TSTree* tree, QsciScintilla* editor);
    // std::vector<QString> getBreadcrumb(TSTree* tree, QsciScintilla* editor, const QByteArray& utf8);

    TSTree *get_m_tree() {
        return m_tree;
    }

    std::vector<int> functionLines;
    void collectFunctionLines(TSNode node);
    void collectFunctionRanges(TSNode node);
    int functionIndexAt(int line, int col) const;
    QVector<FunctionRange> functions;

private:
    // Recursive implementation to traverse the tree
    void traverseAndCollect(TSNode node, const char* targetType, QVector<TSNode> &outList);

    TSParser *m_parser;
    TSTree *m_tree;

    // We must keep the byte array alive because tree-sitter points to this memory
    QByteArray m_sourceBytes;
};

// ✅ NEW: Helper functions for QPlainTextEdit
int getCursorByteOffset(QPlainTextEdit* editor);
TSNode getNodeAtCursor(TSTree* tree, QPlainTextEdit* editor);
std::vector<QString> getBreadcrumb(TSTree* tree, QPlainTextEdit* editor, const QByteArray& sourceUtf8);

int countFunctionDefinitions(const std::string &source);
void traverseAndCount(TSNode node, int &count);

#endif // TREESITTERHELPER_H
