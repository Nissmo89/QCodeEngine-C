#pragma once

#include <vector>
#include <cstdint>
#include <QByteArray>

// tree-sitter C API
#include <tree_sitter/api.h>

struct FoldRange {
    uint32_t startByte  = 0;
    uint32_t endByte    = 0;
    uint32_t startRow   = 0;   // 0-based line of opening delimiter
    uint32_t endRow     = 0;   // 0-based line of closing delimiter
    bool     collapsed  = false;
};

class FoldQuery {
public:
    FoldQuery(const TSLanguage* lang, const QByteArray& schemeSrc);
    ~FoldQuery();

    FoldQuery(const FoldQuery&) = delete;
    FoldQuery& operator=(const FoldQuery&) = delete;

    bool isValid() const;

    std::vector<FoldRange> computeRanges(TSTree* tree,
                                          const QByteArray& sourceUtf8) const;

private:
    TSQuery*  m_query            = nullptr;
    uint32_t  m_foldCaptureIndex = 0;
};
