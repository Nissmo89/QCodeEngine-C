#include "CodeEditor/GutterWidget.h"
#include "FoldManager.h"
#include "CodeEditor_p.h"
#include <QPainter>
#include <QMouseEvent>
#include <QToolTip>

GutterWidget::GutterWidget(QWidget* parent) : QWidget(parent) {
    m_theme = QEditorTheme::draculaTheme();
    setMouseTracking(true); // needed to receive mouseMoveEvent without button pressed
}

void GutterWidget::setEditor(InnerEditor* editor) {
    m_editor = editor;
}

void GutterWidget::setTheme(const QEditorTheme& theme) {
    m_theme = theme;
    update();
}

void GutterWidget::setIconMap(const QMap<int, GutterIconInfo>& icons) {
    m_icons = icons;
    update();
}

void GutterWidget::setFoldManager(FoldManager* foldManager) {
    m_foldManager = foldManager;
}

void GutterWidget::setLineNumbersVisible(bool v) {
    m_showLineNumbers = v;
    update();
}

void GutterWidget::setFoldingVisible(bool v) {
    m_showFolding = v;
    update();
}


void GutterWidget::setCurrentBlock(int blockNumber) {
    if (m_currentBlock != blockNumber) {
        m_currentBlock = blockNumber;
        update();
    }
}

// ---- Layout helpers ----

int GutterWidget::lineNumberWidth() const {
    if (!m_showLineNumbers || !m_editor) return 0;
    int digits = 1;
    int max = qMax(1, m_editor->blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    // 4 padding + digit chars
    int space = 8 + QFontMetrics(m_editor->font()).horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

int GutterWidget::iconX() const {
    return lineNumberWidth() + 2;
}

int GutterWidget::foldArrowX() const {
    return iconX() + (m_icons.isEmpty() ? 0 : iconSize + 2);
}

int GutterWidget::requiredWidth() const {
    int w = 0;
    if (m_showLineNumbers) w += lineNumberWidth();
    w += iconSize + 4;
    if (m_showFolding) w += arrowSize + 4;
    return w;
}

// ---- Drawing helpers ----

void GutterWidget::drawIcon(QPainter& painter, const GutterIconInfo& info, int x, int y, int h) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    int cy = y + h / 2;
    int cx = x + iconSize / 2;

    QColor color;
    if      (info.type == GutterIconType::Error)      color = QColor(0xFF, 0x5C, 0x5C);
    else if (info.type == GutterIconType::Warning)    color = QColor(0xFF, 0xD7, 0x00);
    else if (info.type == GutterIconType::Info)       color = QColor(0x3C, 0xB3, 0xFF);
    else if (info.type == GutterIconType::Breakpoint) color = QColor(0xFF, 0x4C, 0x4C);
    else color = Qt::lightGray;

    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPoint(cx, cy), iconSize/2, iconSize/2);
    painter.restore();
}

void GutterWidget::drawFoldArrow(QPainter& painter, bool folded, int x, int y, int h) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(m_theme.gutterForeground);
    painter.setPen(Qt::NoPen);

    int cy = y + h / 2;
    int cx = x + arrowSize / 2;
    int s  = arrowSize;

    QPolygon poly;
    if (folded) { // right-pointing ▶
        poly << QPoint(cx - s/4, cy - s/3)
             << QPoint(cx - s/4, cy + s/3)
             << QPoint(cx + s/4, cy);
    } else { // down-pointing ▼
        poly << QPoint(cx - s/3, cy - s/4)
             << QPoint(cx + s/3, cy - s/4)
             << QPoint(cx,       cy + s/4);
    }
    painter.drawPolygon(poly);
    painter.restore();
}


// ---- Paint ----

void GutterWidget::paintEvent(QPaintEvent* event) {
    if (!m_editor) return;

    QPainter painter(this);
    painter.fillRect(event->rect(), m_theme.gutterBackground);

    QTextBlock block = m_editor->firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top    = m_editor->blockBoundingGeometry(block).translated(m_editor->contentOffset()).top();
    qreal bottom = top + m_editor->blockBoundingRect(block).height();

    m_foldArrowRects.clear();
    m_iconRects.clear();
    m_lineNumberRects.clear();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            int ht = (int)(bottom - top);
            bool isCurrentLine = (blockNumber == m_currentBlock);
            bool isHovered     = (blockNumber == m_hoveredBlock);

            // ---- Line number ----
            if (m_showLineNumbers) {
                // Highlight background for active line number
                if (isCurrentLine) {
                    QColor hlBg = m_theme.currentLineBackground;
                    painter.fillRect(0, (int)top, lineNumberWidth(), ht, hlBg);
                }

                // Choose number color: active = accent/bright, hovered = slightly brighter, else dim
                QColor numColor;
                if (isCurrentLine)
                    numColor = m_theme.gutterActiveLineNumber;
                else if (isHovered)
                    numColor = m_theme.gutterForeground.lighter(130);
                else
                    numColor = m_theme.gutterForeground;

                painter.setPen(numColor);
                painter.setFont(m_editor->font());
                painter.drawText(0, (int)top, lineNumberWidth() - 4, ht,
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(blockNumber + 1));

                m_lineNumberRects[blockNumber] = QRect(0, (int)top, lineNumberWidth(), ht);
            }


            // ---- Diagnostic icon ----
            if (m_icons.contains(blockNumber)) {
                int ix = iconX();
                drawIcon(painter, m_icons[blockNumber], ix, (int)top, ht);
                m_iconRects[blockNumber] = QRect(ix, (int)top, iconSize, ht);
            }

            // ---- Fold arrow ────────────────────────────────────────────────
            // Show the arrow only when:
            //   • the block is already folded (collapsed indicator must always
            //     be visible so the user knows content is hidden), OR
            //   • the mouse is hovering this exact gutter row.
            // This mirrors VS Code / Zed behaviour and keeps the gutter clean.
            if (m_showFolding && m_foldManager && m_foldManager->isFoldable(blockNumber)) {
                bool folded  = m_foldManager->isFolded(blockNumber);
                bool visible = folded || isHovered;
                if (visible) {
                    int fx = foldArrowX();
                    drawFoldArrow(painter, folded, fx, (int)top, ht);
                    m_foldArrowRects[blockNumber] = QRect(fx, (int)top, arrowSize + 4, ht);
                }
            }
        }

        block = block.next();
        top   = bottom;
        bottom = top + m_editor->blockBoundingRect(block).height();
        ++blockNumber;
    }
}

// ---- Mouse ----

void GutterWidget::mousePressEvent(QMouseEvent* event) {
    // Check fold arrow click
    for (auto it = m_foldArrowRects.begin(); it != m_foldArrowRects.end(); ++it) {
        if (it.value().contains(event->pos())) {
            emit foldClicked(it.key());
            return;
        }
    }

    // Check diagnostic icon click
    for (auto it = m_iconRects.begin(); it != m_iconRects.end(); ++it) {
        if (it.value().contains(event->pos())) {
            emit iconClicked(it.key());
            if (!m_icons[it.key()].tooltip.isEmpty()) {
                QToolTip::showText(event->globalPosition().toPoint(), m_icons[it.key()].tooltip, this);
            }
            return;
        }
    }


    QWidget::mousePressEvent(event);
}

void GutterWidget::mouseMoveEvent(QMouseEvent* event) {
    // Hover detection: use the full gutter width for each row so the fold-arrow
    // column also triggers the hover highlight (previously only the line-number
    // sub-rect was tested, meaning hovering directly over an arrow did nothing).
    int newHovered = -1;
    int fullW = width();
    for (auto it = m_lineNumberRects.begin(); it != m_lineNumberRects.end(); ++it) {
        QRect fullRow(0, it.value().top(), fullW, it.value().height());
        if (fullRow.contains(event->pos())) {
            newHovered = it.key();
            break;
        }
    }
    if (newHovered != m_hoveredBlock) {
        m_hoveredBlock = newHovered;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void GutterWidget::leaveEvent(QEvent* event) {
    if (m_hoveredBlock != -1) {
        m_hoveredBlock = -1;
        update();
    }
    QWidget::leaveEvent(event);
}
