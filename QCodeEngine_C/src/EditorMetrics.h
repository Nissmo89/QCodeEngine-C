#pragma once

#include <QFont>
#include <QFontMetricsF>

namespace EditorMetrics {

static constexpr int kFallbackLineHeight = 22;
static constexpr int kDocumentMargin = 8;
static constexpr int kCursorWidth = 2;

inline int effectiveLineHeight(const QFont& font)
{
    return qMax(1, qRound(QFontMetricsF(font).lineSpacing()));
}

} // namespace EditorMetrics
