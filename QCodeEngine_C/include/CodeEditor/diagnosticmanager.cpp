// DiagnosticManager.cpp
#include "diagnosticmanager.h"
#include <QTextBlock>
#include <QTextLayout>
#include <algorithm>

DiagnosticManager::DiagnosticManager(QObject* parent)
    : QObject(parent) {}

void DiagnosticManager::setDocument(QTextDocument* doc)
{
    m_doc = doc;
}

void DiagnosticManager::setDiagnostics(const QList<Diagnostic>& diagnostics)
{
    clearFromDocument();
    m_diagnostics = diagnostics;
    applyToDocument();
    emit diagnosticsChanged();
}

void DiagnosticManager::clear()
{
    clearFromDocument();
    m_diagnostics.clear();
    emit diagnosticsChanged();
}

// ---------------------------------------------------------------------------
//  applyToDocument
//  Appends WaveUnderline FormatRanges to each affected block's layout,
//  exactly like TreeSitterHighlighter::apply_format() does — so both
//  syntax highlights and squiggles coexist in the same format list.
// ---------------------------------------------------------------------------
void DiagnosticManager::applyToDocument()
{
    if (!m_doc) return;

    for (const Diagnostic& diag : m_diagnostics) {
        QTextBlock block = m_doc->findBlockByNumber(diag.line);
        if (!block.isValid()) continue;

        QTextCharFormat fmt;
        fmt.setUnderlineStyle(QTextCharFormat::WaveUnderline);


        switch (diag.severity) {
        case Diagnostic::Error:   fmt.setUnderlineColor(m_errorColor);   break;
        case Diagnostic::Warning: fmt.setUnderlineColor(m_warningColor); break;
        case Diagnostic::Info:    fmt.setUnderlineColor(m_infoColor);    break;
        case Diagnostic::Hint:    fmt.setUnderlineColor(m_hintColor);    break;
        }

        // Clamp to visible text bounds with a minimum one-char span.
        const int textLength = qMax(1, block.length() - 1);
        const int col = qBound(0, diag.column, textLength - 1);
        const int end = qMin(col + qMax(1, diag.length), textLength);

        applyFormat(fmt, block, col, end);
    }
}

// ---------------------------------------------------------------------------
//  clearFromDocument
//  Strips only the WaveUnderline ranges — leaves syntax highlight ranges
//  (set by TreeSitterHighlighter) untouched.
// ---------------------------------------------------------------------------
void DiagnosticManager::clearFromDocument()
{
    if (!m_doc) return;

    QTextBlock block = m_doc->begin();
    while (block.isValid()) {
        if (!block.layout()) {
            block = block.next();
            continue;
        }
        QList<QTextLayout::FormatRange> ranges = block.layout()->formats();
        bool changed = false;

        ranges.erase(
            std::remove_if(ranges.begin(), ranges.end(),
                           [&changed](const QTextLayout::FormatRange& r) {
                               if (r.format.underlineStyle() == QTextCharFormat::WaveUnderline) {
                                   changed = true;
                                   return true;
                               }
                               return false;
                           }),
            ranges.end()
            );

        if (changed) {
            block.layout()->setFormats(ranges);
            m_doc->markContentsDirty(block.position(), block.length());
        }

        block = block.next();
    }
}

// Matches TreeSitterHighlighter::apply_format() exactly
void DiagnosticManager::applyFormat(const QTextCharFormat& fmt,
                                    QTextBlock block, int start, int end)
{
    if (!m_doc || !block.isValid() || !block.layout())
        return;

    const int textLength = qMax(1, block.length() - 1);
    const int clampedStart = qBound(0, start, textLength - 1);
    const int clampedEnd = qBound(clampedStart + 1, end, textLength);

    QTextLayout::FormatRange r;
    r.start  = clampedStart;
    r.length = clampedEnd - clampedStart;
    r.format = fmt;

    QList<QTextLayout::FormatRange> ranges = block.layout()->formats();
    ranges << r;
    block.layout()->setFormats(ranges);
    m_doc->markContentsDirty(block.position(), block.length());
}
