#pragma once
// ============================================================================
//  FoldManager.h  –  QCodeEngine-C
//
//  Tree-reuse architecture:
//    FoldManager does NOT own a TSParser.  Instead, TreeSitterHighlighter
//    emits parsed(void* treePtr) after every incremental reparse, and
//    updateFoldRanges() receives that already-parsed TSTree*.
//
//    This means the entire codebase performs EXACTLY ONE tree-sitter parse
//    per edit — shared between highlighting and folding — instead of two
//    independent full parses.
//
//  API summary:
//    setDocument(doc)                — wire to editor document once at init
//    updateFoldRanges(tree, doc)     — called from highlighter "parsed" slot
//    isFolded(blockNum)              — is this line a collapsed fold header?
//    isLineHidden(blockNum)          — is this line hidden inside a fold?
//    foldRanges()                    — QMap<startRow, endRow> (0-based)
//    findFoldContaining(blockNum)    — returns startRow or -1
//    toggleFold(blockNum)            — flip one fold; emits foldStateChanged
//    foldAll() / unfoldAll()         — batch; one signal each
//
//  Performance:
//    • isFolded / isLineHidden are O(1) via precomputed QSet caches.
//    • toggleFold / findFoldContaining are O(1) via QHash index.
//    • applyFoldsToDocument is O(lines) — one lookup per block.
// ============================================================================
#include <memory>
#include <QObject>
#include <QMap>
#include <QSet>
#include <QHash>
#include <QTextDocument>
#include <tree_sitter/api.h>
#include "CodeEditor/FoldQuery.h"

class FoldManager : public QObject {
    Q_OBJECT
public:
    explicit FoldManager(QObject* parent = nullptr);
    ~FoldManager() override = default;

    FoldManager(const FoldManager&)            = delete;
    FoldManager& operator=(const FoldManager&) = delete;

    // ── Setup ────────────────────────────────────────────────────────────────
    void setDocument(QTextDocument* doc);

    // ── Called from TreeSitterHighlighter::parsed slot ───────────────────────
    // Receives the already-parsed TSTree* (no second parse needed).
    // Preserves existing collapsed state across calls.
    void updateFoldRanges(void* treePtr, QTextDocument* doc);

    // ── O(1) queries ─────────────────────────────────────────────────────────
    bool isFolded      (int blockNumber) const; // collapsed fold header?
    bool isLineHidden  (int blockNumber) const; // inside a collapsed fold?
    int  findFoldContaining(int blockNumber) const; // startRow, or -1

    // startRow → endRow map (0-based).  Used by gutter to draw arrows.
    const QMap<int,int>& foldRanges() const { return m_ranges; }

    // ── Mutations ────────────────────────────────────────────────────────────
    void toggleFold(int blockNumber);           // emits foldStateChanged
    void foldAll();                             // batch; one signal
    void unfoldAll();                           // batch; one signal

signals:
    void foldRangesUpdated();   // new ranges computed (from updateFoldRanges)
    void foldStateChanged();    // a collapse/expand happened (gutter/doc update)

private:
    void rebuildCaches();
    void applyFoldsToDocument();

    QTextDocument* m_doc = nullptr;

    QMap<int, int>   m_ranges;          // startRow → endRow (0-based)
    QSet<int>        m_collapsed;       // startRows currently collapsed

    // O(1) caches — rebuilt by rebuildCaches()
    QSet<int>        m_foldHeaders;     // set of startRow values
    QHash<int,int>   m_startToEnd;      // startRow → endRow
    QHash<int,int>   m_lineToFoldStart; // hidden line → its fold's startRow
    QSet<int>        m_hiddenLines;     // all hidden block numbers

    std::unique_ptr<FoldQuery> m_query;
};
