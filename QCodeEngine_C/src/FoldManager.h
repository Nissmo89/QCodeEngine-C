#pragma once
#include <QObject>
#include <QMap>
#include <QSet>

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
    QMap<int, int> foldRanges() const;

    // Returns the start-block of whichever active fold *contains* blockNumber
    // (i.e. blockNumber is hidden inside that fold). Returns -1 if none.
    int findFoldContaining(int blockNumber) const;

signals:
    void foldChanged(int blockNumber, bool folded);
    void foldRangesUpdated();

private:
    void recomputeVisibility();
    void collectFoldRanges(void* nodeRaw, QMap<int,int>& out);

    QMap<int, int> m_foldRanges; // startBlock (0-based) -> endBlock (0-based)
    QSet<int> m_foldedBlocks;
    QTextDocument* m_doc = nullptr;
};
