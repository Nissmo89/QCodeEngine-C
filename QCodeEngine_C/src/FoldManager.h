#pragma once

#include <memory>
#include <vector>
#include <QObject>
#include <QString>
#include <tree_sitter/api.h>
#include "CodeEditor/FoldQuery.h"

class FoldManager : public QObject {
    Q_OBJECT
public:
    explicit FoldManager(QObject* parent = nullptr);
    ~FoldManager() override;

    FoldManager(const FoldManager&) = delete;
    FoldManager& operator=(const FoldManager&) = delete;

    void reparse(const QString& source);

    bool             hasFoldAt(int line) const;
    bool             isLineHidden(int line) const;
    void             toggleFold(int line);
    const FoldRange* foldAt(int line) const;
    const std::vector<FoldRange>& ranges() const;

signals:
    void foldsChanged();

private:
    TSParser*                  m_parser = nullptr;
    TSTree*                    m_tree   = nullptr;
    std::unique_ptr<FoldQuery> m_query;
    std::vector<FoldRange>     m_ranges;
};
