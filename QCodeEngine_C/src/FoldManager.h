#pragma once
#include <QObject>
#include <QMap>
#include <QList>
#include <QTextCursor>

struct TSTree;
class QTextDocument;

class FoldManager : public QObject {
    Q_OBJECT
public:
    explicit FoldManager(QObject* parent = nullptr) : QObject(parent) {}

    // Called by highlighter after each parse with the new tree pointer.
    void updateFoldRanges(void* tree, QTextDocument* doc);

    void setDocument(QTextDocument* doc) { m_doc = doc; }

    bool isFoldable(int blockNumber) const;
    bool isFolded(int blockNumber) const;
    void toggleFold(int blockNumber);
    void foldAll();
    void unfoldAll();

    int foldEndBlock(int blockNumber) const;

signals:
    void foldChanged(int blockNumber, bool folded);
    void foldRangesUpdated();

private:
    void applyFolds(const QList<QPair<int, int>>& toShow, const QList<QPair<int, int>>& toHide);
    void collectFoldRanges(void* nodeRaw, QMap<int,int>& out);

    QMap<int, int> m_foldRanges; // startBlock (0-based) -> endBlock (0-based)
    
    struct ActiveFold {
        QTextCursor start;
        QTextCursor end;
    };
    QList<ActiveFold> m_activeFolds;
    QTextDocument* m_doc = nullptr;
};
