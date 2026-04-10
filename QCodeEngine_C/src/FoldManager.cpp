#include "FoldManager.h"
#include <QTextDocument>
#include <QTextBlock>
#include <QPlainTextDocumentLayout>
#include <tree_sitter/api.h>
#include <QSet>

bool FoldManager::isFoldable(int blockNumber) const {
    return m_foldRanges.contains(blockNumber);
}

bool FoldManager::isFolded(int blockNumber) const {
    for (const auto& f : m_activeFolds) {
        if (f.start.blockNumber() == blockNumber) return true;
    }
    return false;
}

void FoldManager::toggleFold(int blockNumber) {
    if (!isFoldable(blockNumber) || !m_doc) return;

    bool folded = false;
    int idxToRemove = -1;
    for (int i = 0; i < m_activeFolds.size(); ++i) {
        if (m_activeFolds[i].start.blockNumber() == blockNumber) {
            idxToRemove = i;
            break;
        }
    }

    if (idxToRemove >= 0) {
        // Unfold
        ActiveFold f = m_activeFolds.takeAt(idxToRemove);
        applyFolds({qMakePair(f.start.blockNumber(), f.end.blockNumber())}, {});
        folded = false;
    } else {
        // Fold
        int endBlock = m_foldRanges[blockNumber];
        ActiveFold f;
        f.start = QTextCursor(m_doc->findBlockByNumber(blockNumber));
        f.end = QTextCursor(m_doc->findBlockByNumber(endBlock));
        m_activeFolds.append(f);
        applyFolds({}, {qMakePair(blockNumber, endBlock)});
        folded = true;
    }
    emit foldChanged(blockNumber, folded);
}

int FoldManager::foldEndBlock(int blockNumber) const {
    return m_foldRanges.value(blockNumber, -1);
}

int FoldManager::findFoldContaining(int blockNumber) const {
    for (const auto& f : m_activeFolds) {
        int s = f.start.blockNumber();
        int e = f.end.blockNumber();
        // blockNumber is *inside* the fold if it is strictly after the start
        // and at-or-before the end (the end line itself is hidden too).
        if (blockNumber > s && blockNumber <= e)
            return s;
    }
    return -1;
}

void FoldManager::foldAll() {
    if (!m_doc) return;
    QList<QPair<int, int>> toHide;
    for (auto it = m_foldRanges.begin(); it != m_foldRanges.end(); ++it) {
        if (!isFolded(it.key())) {
            ActiveFold f;
            f.start = QTextCursor(m_doc->findBlockByNumber(it.key()));
            f.end = QTextCursor(m_doc->findBlockByNumber(it.value()));
            m_activeFolds.append(f);
            toHide.append(qMakePair(it.key(), it.value()));
        }
    }
    applyFolds({}, toHide);
}

void FoldManager::unfoldAll() {
    QList<QPair<int, int>> toShow;
    for (const auto& f : m_activeFolds) {
        toShow.append(qMakePair(f.start.blockNumber(), f.end.blockNumber()));
    }
    m_activeFolds.clear();
    applyFolds(toShow, {});
}

void FoldManager::updateFoldRanges(void* tree, QTextDocument* doc) {
    if (!doc) return;

    QMap<int, int> newRanges;
    if (tree) {
        TSTree* tsTree = static_cast<TSTree*>(tree);
        TSNode root = ts_tree_root_node(tsTree);
        collectFoldRanges(&root, newRanges);
    }

    QList<QPair<int, int>> toShow;
    QList<QPair<int, int>> toHide;
    
    QList<ActiveFold> keptFolds;
    for (int i = 0; i < m_activeFolds.size(); ++i) {
        ActiveFold& f = m_activeFolds[i];
        int s = f.start.blockNumber();
        int oldEnd = f.end.blockNumber();

        if (newRanges.contains(s)) {
            int newEnd = newRanges[s];
            if (newEnd != oldEnd) {
                // End moved. Show old range, hide new range.
                toShow.append(qMakePair(s, oldEnd));
                toHide.append(qMakePair(s, newEnd));
                f.end = QTextCursor(doc->findBlockByNumber(newEnd));
            } else {
                // Make sure it remains hidden (in case typing temporarily exposed it)
                toHide.append(qMakePair(s, newEnd));
            }
            keptFolds.append(f);
        } else {
            // Fold no longer valid syntax
            toShow.append(qMakePair(s, oldEnd));
        }
    }
    
    m_activeFolds = keptFolds;
    m_foldRanges = newRanges;

    applyFolds(toShow, toHide);
    emit foldRangesUpdated();
}

void FoldManager::collectFoldRanges(void* nodeRaw, QMap<int,int>& out) {
    TSNode* pNode = static_cast<TSNode*>(nodeRaw);
    TSNode node = *pNode;

    // NOTE: "compound_statement" is intentionally excluded here.
    // It is always a child of one of the parent nodes below (function_definition,
    // if_statement, etc.) and starts on the same line.  Including it caused
    // duplicate fold arrows for K&R-style code where the opening brace lives on
    // its own line (both the parent node and the compound_statement would each
    // register a foldable entry for adjacent lines).
    static const QSet<QString> FOLDABLE_TYPES = {
        "function_definition",
        "struct_specifier",
        "union_specifier",
        "enum_specifier",
        "if_statement",
        "for_statement",
        "while_statement",
        "do_statement",
        "switch_statement",
        "comment",
        "preproc_if",
    };

    QString nodeType = QString::fromUtf8(ts_node_type(node));
    if (FOLDABLE_TYPES.contains(nodeType)) {
        int startLine = (int)ts_node_start_point(node).row;
        int endLine   = (int)ts_node_end_point(node).row;
        if (endLine > startLine) {
            auto existing = out.find(startLine);
            if (existing == out.end() || existing.value() < endLine)
                out[startLine] = endLine;
        }
    }

    uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_child(node, i);
        collectFoldRanges(&child, out);
    }
}

void FoldManager::applyFolds(const QList<QPair<int, int>>& toShow, const QList<QPair<int, int>>& toHide) {
    if (!m_doc) return;

    int dirtyFrom = -1;
    int dirtyTo = -1;

    auto process = [&](int startBlock, int endBlock, bool hide) {
        if (startBlock < 0 || endBlock <= startBlock) return;
        QTextBlock block = m_doc->findBlockByNumber(startBlock + 1);
        while (block.isValid() && block.blockNumber() <= endBlock) {
            if (block.isVisible() == hide) {
                block.setVisible(!hide);
                int bPos = block.position();
                int bEnd = bPos + block.length();
                if (dirtyFrom < 0 || bPos < dirtyFrom) dirtyFrom = bPos;
                if (bEnd > dirtyTo) dirtyTo = bEnd;
            }
            block = block.next();
        }
    };

    for (const auto& p : toShow) process(p.first, p.second, false);
    for (const auto& p : toHide) process(p.first, p.second, true);

    if (dirtyFrom >= 0) {
        m_doc->markContentsDirty(dirtyFrom, dirtyTo - dirtyFrom);
        if (auto* layout = qobject_cast<QPlainTextDocumentLayout*>(m_doc->documentLayout()))
            layout->requestUpdate();
    }
}
