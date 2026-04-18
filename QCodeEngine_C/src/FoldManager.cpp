// ============================================================================
//  FoldManager.cpp  –  QCodeEngine-C
//
//  Tree-reuse design — no owned TSParser.
//  The TSTree* is borrowed from TreeSitterHighlighter for the duration of
//  updateFoldRanges(); the highlighter continues to own it.
// ============================================================================
#include "FoldManager.h"
#include <QTextBlock>
#include <QDebug>

extern "C" { const TSLanguage* tree_sitter_c(void); }

// ---------------------------------------------------------------------------
FoldManager::FoldManager(QObject* parent)
    : QObject(parent)
{
    m_query = std::make_unique<FoldQuery>(tree_sitter_c());
    if (!m_query->isValid())
        qCritical() << "FoldManager: FoldQuery invalid — folding disabled";
}

void FoldManager::setDocument(QTextDocument* doc)
{
    m_doc = doc;
}

// ---------------------------------------------------------------------------
//  updateFoldRanges
//  Called every time TreeSitterHighlighter finishes a parse.
//  We borrow the TSTree* — cast it, run our query, then release.
// ---------------------------------------------------------------------------
void FoldManager::updateFoldRanges(void* treePtr, QTextDocument* doc)
{
    if (!treePtr) return;

    // Accept a document update (e.g. setText() wires a new document)
    if (doc && doc != m_doc) m_doc = doc;

    TSTree* tree = static_cast<TSTree*>(treePtr);

    // Run fold query on the already-parsed tree (zero extra parsing cost)
    std::vector<FoldRange> raw;
    if (m_query && m_query->isValid())
        raw = m_query->computeRanges(tree);

    // Build new QMap, preserving collapsed state from previous generation
    QMap<int,int> newRanges;
    for (const FoldRange& fr : raw)
        newRanges.insert(static_cast<int>(fr.startRow),
                         static_cast<int>(fr.endRow));

    // Preserve only collapsed starts that still exist in the new ranges
    QSet<int> prevCollapsed = m_collapsed;
    m_collapsed.clear();
    for (int s : prevCollapsed)
        if (newRanges.contains(s))
            m_collapsed.insert(s);

    m_ranges = std::move(newRanges);
    rebuildCaches();
    applyFoldsToDocument();
    emit foldRangesUpdated();
}

// ---------------------------------------------------------------------------
//  rebuildCaches — called after every state change
// ---------------------------------------------------------------------------
void FoldManager::rebuildCaches()
{
    m_foldHeaders.clear();
    m_startToEnd.clear();
    m_lineToFoldStart.clear();
    m_hiddenLines.clear();

    for (auto it = m_ranges.begin(); it != m_ranges.end(); ++it) {
        const int start = it.key();
        const int end   = it.value();
        m_foldHeaders.insert(start);
        m_startToEnd.insert(start, end);

        if (m_collapsed.contains(start)) {
            // Lines strictly between start and end are hidden.
            // The closing brace line (end) stays visible — matches Zed/VS Code.
            for (int row = start + 1; row < end; ++row) {
                m_hiddenLines.insert(row);
                m_lineToFoldStart.insert(row, start);
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  applyFoldsToDocument — O(lines), one O(1) lookup per block
// ---------------------------------------------------------------------------
void FoldManager::applyFoldsToDocument()
{
    if (!m_doc) return;

    QTextBlock block = m_doc->begin();
    while (block.isValid()) {
        const bool hidden = m_hiddenLines.contains(block.blockNumber());
        if (block.isVisible() == hidden) {
            block.setVisible(!hidden);
            block.setLineCount(hidden ? 0 : 1);
        }
        block = block.next();
    }
    m_doc->markContentsDirty(0, m_doc->characterCount());
}

// ---------------------------------------------------------------------------
//  O(1) queries
// ---------------------------------------------------------------------------
bool FoldManager::isFolded(int blockNumber) const
{
    // "folded" means: is a fold header AND is currently collapsed
    return m_foldHeaders.contains(blockNumber)
           && m_collapsed.contains(blockNumber);
}

bool FoldManager::isLineHidden(int blockNumber) const
{
    return m_hiddenLines.contains(blockNumber);
}

int FoldManager::findFoldContaining(int blockNumber) const
{
    // Returns the startRow of the innermost collapsed fold that contains
    // blockNumber, or -1.  Used by the auto-unfold guard.
    auto it = m_lineToFoldStart.find(blockNumber);
    return (it != m_lineToFoldStart.end()) ? it.value() : -1;
}

// ---------------------------------------------------------------------------
//  Mutations
// ---------------------------------------------------------------------------
void FoldManager::toggleFold(int blockNumber)
{
    if (!m_foldHeaders.contains(blockNumber)) return;

    if (m_collapsed.contains(blockNumber))
        m_collapsed.remove(blockNumber);
    else
        m_collapsed.insert(blockNumber);

    rebuildCaches();
    applyFoldsToDocument();
    emit foldStateChanged();
}

void FoldManager::foldAll()
{
    bool changed = false;
    for (auto it = m_ranges.begin(); it != m_ranges.end(); ++it) {
        if (!m_collapsed.contains(it.key())) {
            m_collapsed.insert(it.key());
            changed = true;
        }
    }
    if (changed) {
        rebuildCaches();
        applyFoldsToDocument();
        emit foldStateChanged();
    }
}

void FoldManager::unfoldAll()
{
    if (m_collapsed.isEmpty()) return;
    m_collapsed.clear();
    rebuildCaches();
    applyFoldsToDocument();
    emit foldStateChanged();
}
