#include "CodeEditor/MiniMapWidget.h"
#include "EditorMetrics.h"

#include <QPlainTextEdit>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QMouseEvent>
#include <qplaintextedit.h>

namespace {

static int clampEditorLine(int line, int totalLines)
{
    return qBound(0, line, qMax(0, totalLines - 1));
}

static int effectiveEditorLineHeight(const QPlainTextEdit* editor)
{
    if (!editor)
        return EditorMetrics::kFallbackLineHeight;

    return EditorMetrics::effectiveLineHeight(editor->font());
}

} // namespace

MiniMapWidget::MiniMapWidget(QPlainTextEdit* editor, QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setMiniMapWidth(m_width);
    setEditor(editor);
}

void MiniMapWidget::setEditor(QPlainTextEdit* editor)
{
    if (m_editor == editor)
        return;

    if (m_editor) {
        disconnect(m_editor, nullptr, this, nullptr);
        if (m_editor->verticalScrollBar())
            disconnect(m_editor->verticalScrollBar(), nullptr, this, nullptr);
        if (m_editor->document())
            disconnect(m_editor->document(), nullptr, this, nullptr);
    }

    m_editor = editor;
    reconnectEditorSignals();
    update();
}

void MiniMapWidget::setTheme(const QEditorTheme& theme)
{
    m_background = theme.gutterBackground.darker(112);
    m_lineColor = theme.tokenComment;
    m_lineColor.setAlpha(130);
    m_viewportFill = theme.selectionBackground;
    m_viewportFill.setAlpha(85);
    m_viewportBorder = theme.accent;
    m_viewportBorder.setAlpha(190);
    update();
}

void MiniMapWidget::setMiniMapWidth(int width)
{
    m_width = qMax(72, width);
    setMinimumWidth(m_width);
    setMaximumWidth(m_width);
    updateGeometry();
}

QSize MiniMapWidget::sizeHint() const
{
    return QSize(m_width, 220);
}

void MiniMapWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), m_background.isValid() ? m_background : QColor(28, 30, 34));

    if (!m_editor || !m_editor->document())
        return;

    const int totalLines = qMax(1, m_editor->document()->blockCount());
    const QRect contentRect = rect().adjusted(2, 2, -2, -2);
    const int h = qMax(1, contentRect.height());
    const int w = qMax(1, contentRect.width());

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_lineColor.isValid() ? m_lineColor : QColor(120, 130, 145, 140));

    const double linesPerPixel = static_cast<double>(totalLines) / static_cast<double>(h);
    for (int py = 0; py < h; ++py) {
        const int line = clampEditorLine(static_cast<int>(py * linesPerPixel), totalLines);
        const QTextBlock block = m_editor->document()->findBlockByNumber(line);
        const int len = block.isValid() ? block.text().size() : 0;
        const int lineWidth = qBound(3, 3 + (len * (w - 4)) / 220, w);
        painter.drawRect(contentRect.left(), contentRect.top() + py, lineWidth, 1);
    }

    const QTextCursor topCursor = m_editor->cursorForPosition(QPoint(0, 0));
    const int firstVisibleLine = qMax(0, topCursor.blockNumber());
    const int lineHeight = qMax(1, effectiveEditorLineHeight(m_editor));
    const int visibleLines = qMax(1, m_editor->viewport()->height() / lineHeight + 1);

    const int topY = contentRect.top()
                     + static_cast<int>((static_cast<double>(firstVisibleLine) / totalLines) * h);
    const int bottomLine = qMin(totalLines, firstVisibleLine + visibleLines);
    const int bottomY = contentRect.top()
                        + static_cast<int>((static_cast<double>(bottomLine) / totalLines) * h);
    const int viewportHeight = qMax(8, bottomY - topY);

    QRect viewportRect(contentRect.left(), topY, contentRect.width(), viewportHeight);
    viewportRect = viewportRect.intersected(contentRect);

    painter.setBrush(m_viewportFill.isValid() ? m_viewportFill : QColor(90, 120, 180, 85));
    painter.setPen(QPen(m_viewportBorder.isValid() ? m_viewportBorder : QColor(115, 150, 220, 200), 1));
    painter.drawRect(viewportRect);
}

void MiniMapWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        navigateToY(static_cast<int>(event->position().y()));
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void MiniMapWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        navigateToY(static_cast<int>(event->position().y()));
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void MiniMapWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

int MiniMapWidget::lineForY(int y) const
{
    if (!m_editor || !m_editor->document())
        return 0;

    const int totalLines = qMax(1, m_editor->document()->blockCount());
    const QRect contentRect = rect().adjusted(2, 2, -2, -2);
    const int localY = qBound(0, y - contentRect.top(), qMax(0, contentRect.height()));
    const int line = static_cast<int>(
        (static_cast<double>(localY) / qMax(1, contentRect.height())) * totalLines);
    return clampEditorLine(line, totalLines);
}

void MiniMapWidget::navigateToY(int y)
{
    if (!m_editor || !m_editor->document())
        return;

    const int line = lineForY(y);
    const QTextBlock block = m_editor->document()->findBlockByNumber(line);
    if (!block.isValid())
        return;

    QTextCursor cursor(block);
    m_editor->setTextCursor(cursor);
    m_editor->centerCursor();
}

void MiniMapWidget::reconnectEditorSignals()
{
    if (!m_editor)
        return;

    if (m_editor->document()) {
        connect(m_editor->document(), &QTextDocument::contentsChanged,
                this, qOverload<>(&MiniMapWidget::update));
    }
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged,
            this, qOverload<>(&MiniMapWidget::update));
    connect(m_editor, &QPlainTextEdit::updateRequest,
            this, [this](const QRect&, int) { update(); });
    if (m_editor->verticalScrollBar()) {
        connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged,
                this, qOverload<>(&MiniMapWidget::update));
        connect(m_editor->verticalScrollBar(), &QScrollBar::rangeChanged,
                this, [this](int, int) { update(); });
    }
}
