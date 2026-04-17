#include "FoldManager.h"
#include <QTextDocument>
#include <QTextBlock>
#include <QPlainTextDocumentLayout>
#include <tree_sitter/api.h>

bool FoldManager::isFoldable(int blockNumber) const {
    return m_foldRanges.contains(blockNumber);
}

bool FoldManager::isFolded(int blockNumber) const {
    return m_foldedBlocks.contains(blockNumber);
}

void FoldManager::toggleFold(int blockNumber) {
    if (!isFoldable(blockNumber) || !m_doc) return;

    const bool folded = m_foldedBlocks.contains(blockNumber);
    if (folded) {
        m_foldedBlocks.remove(blockNumber);
    } else {
        m_foldedBlocks.insert(blockNumber);
    }

    recomputeVisibility();
    emit foldChanged(blockNumber, !folded);
}

int FoldManager::foldEndBlock(int blockNumber) const {
    return m_foldRanges.value(blockNumber, -1);
}

QMap<int, int> FoldManager::foldRanges() const {
    return m_foldRanges;
}

int FoldManager::findFoldContaining(int blockNumber) const {
    int result = -1;
    for (int start : m_foldedBlocks) {
        int end = m_foldRanges.value(start, -1);
        if (blockNumber > start && blockNumber <= end) {
            if (result < 0 || start > result)
                result = start;
        }
    }
    return result;
}

void FoldManager::foldAll() {
    if (!m_doc) return;
    for (auto it = m_foldRanges.begin(); it != m_foldRanges.end(); ++it) {
        m_foldedBlocks.insert(it.key());
    }
    recomputeVisibility();
}

void FoldManager::unfoldAll() {
    if (!m_doc) return;
    m_foldedBlocks.clear();
    recomputeVisibility();
}

void FoldManager::updateFoldRanges(void* tree, QTextDocument* doc) {
    if (!doc) return;
    m_doc = doc;

    QMap<int, int> newRanges;
    if (tree) {
        TSTree* tsTree = static_cast<TSTree*>(tree);
        TSNode root = ts_tree_root_node(tsTree);
        collectFoldRanges(&root, newRanges);
    }

    QSet<int> newFolded;
    for (int start : m_foldedBlocks) {
        if (newRanges.contains(start)) {
            newFolded.insert(start);
        }
    }

    m_foldRanges = std::move(newRanges);
    m_foldedBlocks = std::move(newFolded);

    recomputeVisibility();
    emit foldRangesUpdated();
}

void FoldManager::recomputeVisibility() {
    if (!m_doc) return;

    QTextBlock block = m_doc->begin();
    while (block.isValid()) {
        const int blockNumber = block.blockNumber();
        bool visible = true;
        for (int start : m_foldedBlocks) {
            const int end = m_foldRanges.value(start, -1);
            if (blockNumber > start && blockNumber <= end) {
                visible = false;
                break;
            }
        }

        if (block.isVisible() != visible) {
            block.setVisible(visible);
        }
        block = block.next();
    }

    m_doc->markContentsDirty(0, m_doc->characterCount());
    if (auto* layout = qobject_cast<QPlainTextDocumentLayout*>(m_doc->documentLayout()))
        layout->requestUpdate();
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
    static const QSet<QString> SIGNATURE_NODES = {
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
    if (SIGNATURE_NODES.contains(nodeType)) {
        int startLine = (int)ts_node_start_point(node).row;
        int endLine = (int)ts_node_end_point(node).row;
        for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
            TSNode child = ts_node_child(node, i);
            if (QString::fromUtf8(ts_node_type(child)) == "compound_statement") {
                startLine = (int)ts_node_start_point(child).row;
                break;
            }
        }
        if (endLine > startLine) {
            auto existing = out.find(startLine);
            if (existing == out.end() || existing.value() < endLine)
                out[startLine] = endLine;
        }
    }

    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_child(node, i);
        collectFoldRanges(&child, out);
    }
}
