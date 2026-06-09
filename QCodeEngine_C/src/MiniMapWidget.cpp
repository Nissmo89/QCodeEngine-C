#include "CodeEditor/MiniMapWidget.h"
#include "CodeEditor/diagnosticmanager.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTextDocument>

namespace {

static constexpr int kOverviewScrollbarWidth = 14;
static constexpr int kOverviewScrollbarMinWidth = 12;
static constexpr int kDefaultScrollbarWidth = 12;
static constexpr int kBorderWidth = 1;
static constexpr int kThumbMinHeight = 26;
static constexpr int kMarkerHeight = 3;
static constexpr int kCaretHeight = 4;

static int clampEditorLine(int line, int totalLines) {
  return qBound(0, line, qMax(0, totalLines - 1));
}

static QRect contentRectForWidget(const QRect &widgetRect) {
  return widgetRect.adjusted(kBorderWidth, 0, 0, 0);
}

static QRect trackRectForContent(const QRect &contentRect) {
  if (contentRect.isEmpty())
    return {};

  return contentRect;
}

static int markerYForLine(int line, int totalLines, const QRect &contentRect) {
  if (contentRect.isEmpty())
    return 0;

  if (totalLines <= 1 || contentRect.height() <= 1)
    return contentRect.top();

  const qreal ratio = static_cast<qreal>(clampEditorLine(line, totalLines)) /
                      static_cast<qreal>(totalLines - 1);
  return contentRect.top() + qRound(ratio * (contentRect.height() - 1));
}

static QRect markerRectForY(const QRect &contentRect, int y, int height) {
  if (contentRect.isEmpty())
    return {};

  QRect markerRect(contentRect.left(), y - (height / 2), contentRect.width(),
                   qMax(1, height));
  return markerRect.intersected(contentRect);
}

static QRect thumbRectForScrollBar(const QScrollBar *scrollBar,
                                   const QRect &contentRect,
                                   const QRect &trackRect) {
  if (!scrollBar || contentRect.isEmpty() || trackRect.isEmpty())
    return {};

  const int minimum = scrollBar->minimum();
  const int maximum = scrollBar->maximum();
  const int pageStep = qMax(1, scrollBar->pageStep());
  const int trackHeight = qMax(1, contentRect.height());
  const int totalRange = qMax(1, (maximum - minimum) + pageStep);
  const int thumbHeight =
      qBound(qMin(kThumbMinHeight, trackHeight),
             qRound((static_cast<qreal>(pageStep) / totalRange) * trackHeight),
             trackHeight);

  const int available = qMax(0, trackHeight - thumbHeight);
  qreal ratio = 0.0;
  if (maximum > minimum) {
    ratio = static_cast<qreal>(scrollBar->sliderPosition() - minimum) /
            static_cast<qreal>(maximum - minimum);
  }
  ratio = qBound<qreal>(0.0, ratio, 1.0);

  const int top = contentRect.top() + qRound(ratio * available);
  return QRect(trackRect.left(), top, trackRect.width(), thumbHeight)
      .intersected(contentRect);
}

} // namespace

MiniMapWidget::MiniMapWidget(QPlainTextEdit *editor, QWidget *parent)
    : QScrollBar(Qt::Vertical, parent) {
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setMouseTracking(true);
  setFocusPolicy(Qt::NoFocus);
  m_overviewWidth = kOverviewScrollbarWidth;
  m_scrollBarWidth = kDefaultScrollbarWidth;
  applyWidth();

  connect(this, &QScrollBar::valueChanged, this,
          qOverload<>(&MiniMapWidget::update));
  connect(this, &QScrollBar::rangeChanged, this,
          [this](int, int) { update(); });
  connect(this, &QScrollBar::sliderMoved, this, [this](int) { update(); });

  setEditor(editor);
}

void MiniMapWidget::setEditor(QPlainTextEdit *editor) {
  if (m_editor == editor)
    return;

  if (m_editor) {
    disconnect(m_editor, nullptr, this, nullptr);
    if (m_editor->document())
      disconnect(m_editor->document(), nullptr, this, nullptr);
  }

  m_editor = editor;
  reconnectEditorSignals();
  update();
}

void MiniMapWidget::setDiagnosticManager(DiagnosticManager *diagnosticManager) {
  if (m_diagnosticManager == diagnosticManager)
    return;

  if (m_diagnosticManager)
    disconnect(m_diagnosticManager, nullptr, this, nullptr);

  m_diagnosticManager = diagnosticManager;
  if (m_diagnosticManager) {
    connect(m_diagnosticManager, &DiagnosticManager::diagnosticsChanged, this,
            qOverload<>(&MiniMapWidget::update));
  }

  update();
}

void MiniMapWidget::setTheme(const QEditorTheme &theme) {
  m_background = theme.minimapBackground.isValid()
                     ? theme.minimapBackground
                     : theme.gutterBackground.darker(108);

  m_borderColor = theme.minimapBorderColor;
  if (!m_borderColor.isValid())
    m_borderColor = QColor(70, 76, 84, 220);

  m_trackColor = theme.minimapTrackColor;
  if (!m_trackColor.isValid())
    m_trackColor = QColor(80, 86, 95, 95);

  m_thumbColor = theme.minimapViewportColor.isValid()
                     ? theme.minimapViewportColor
                     : theme.selectionBackground;
  if (!m_thumbColor.isValid())
    m_thumbColor = QColor(110, 118, 129, 120);

  m_caretColor = theme.minimapCaretColor.isValid() ? theme.minimapCaretColor
                                                   : QColor(84, 174, 255);
  m_errorColor = theme.minimapErrorColor.isValid() ? theme.minimapErrorColor
                                                   : QColor(224, 76, 76);
  m_warningColor = theme.minimapWarningColor.isValid() ? theme.minimapWarningColor
                                                       : QColor(236, 169, 62);
  update();
}

void MiniMapWidget::setOverviewVisible(bool visible) {
  if (m_overviewVisible == visible)
    return;

  m_overviewVisible = visible;
  applyWidth();
  update();
}

void MiniMapWidget::setMiniMapWidth(int width) {
  m_overviewWidth = qMax(kOverviewScrollbarMinWidth, width);
  applyWidth();
}

QSize MiniMapWidget::sizeHint() const {
  const int width = m_overviewVisible ? m_overviewWidth : m_scrollBarWidth;
  return QSize(width, 220);
}

QSize MiniMapWidget::minimumSizeHint() const { return sizeHint(); }

void MiniMapWidget::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);

  QPainter painter(this);
  painter.fillRect(rect(),
                   m_background.isValid() ? m_background : QColor(24, 26, 29));
  painter.fillRect(QRect(rect().left(), rect().top(), 1, rect().height()),
                   m_borderColor.isValid() ? m_borderColor
                                           : QColor(70, 76, 84, 150));

  const QRect contentRect = contentRectForWidget(rect());
  const QRect trackRect = trackRectForContent(contentRect);
  if (contentRect.isEmpty() || trackRect.isEmpty())
    return;

  const QColor trackColor =
      m_trackColor.isValid() ? m_trackColor : QColor(80, 86, 95, 95);

  painter.setPen(Qt::NoPen);
  painter.setBrush(trackColor);
  painter.drawRect(trackRect);

  if (!m_overviewVisible || !m_editor || !m_editor->document()) {
    const QRect thumbRect = thumbRectForScrollBar(this, contentRect, trackRect);
    if (!thumbRect.isEmpty()) {
      painter.setBrush(m_thumbColor.isValid() ? m_thumbColor
                                              : QColor(110, 118, 129, 120));
      painter.drawRect(thumbRect);
    }
    return;
  }

  const int totalLines = qMax(1, m_editor->document()->blockCount());
  if (m_diagnosticManager) {
    const QList<Diagnostic> &diagnostics = m_diagnosticManager->diagnostics();
    for (const Diagnostic &diagnostic : diagnostics) {
      QColor markerColor;
      switch (diagnostic.severity) {
      case Diagnostic::Error:
        markerColor = m_errorColor;
        break;
      case Diagnostic::Warning:
        markerColor = m_warningColor;
        break;
      default:
        continue;
      }

      const int markerY =
          markerYForLine(diagnostic.line, totalLines, contentRect);
      const QRect markerRect =
          markerRectForY(trackRect, markerY, kMarkerHeight);
      painter.setBrush(markerColor);
      painter.drawRect(markerRect);
    }
  }

  const int caretLine = m_editor->textCursor().blockNumber();
  const int caretY = markerYForLine(caretLine, totalLines, contentRect);
  const QRect caretRect = markerRectForY(trackRect, caretY, kCaretHeight);
  painter.setBrush(m_caretColor.isValid() ? m_caretColor
                                          : QColor(84, 174, 255));
  painter.drawRect(caretRect);

  const QRect thumbRect = thumbRectForScrollBar(this, contentRect, trackRect);
  if (!thumbRect.isEmpty()) {
    const QColor thumbColor =
        m_thumbColor.isValid() ? m_thumbColor : QColor(110, 118, 129, 120);
    painter.setBrush(thumbColor);
    painter.drawRect(thumbRect);
  }
}

void MiniMapWidget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_dragging = true;
    setSliderDown(true);
    scrollToY(static_cast<int>(event->position().y()));
    event->accept();
    return;
  }

  QScrollBar::mousePressEvent(event);
}

void MiniMapWidget::mouseMoveEvent(QMouseEvent *event) {
  if (m_dragging) {
    scrollToY(static_cast<int>(event->position().y()));
    event->accept();
    return;
  }

  QScrollBar::mouseMoveEvent(event);
}

void MiniMapWidget::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && m_dragging) {
    m_dragging = false;
    setSliderDown(false);
    event->accept();
    return;
  }

  QScrollBar::mouseReleaseEvent(event);
}

int MiniMapWidget::scrollValueForY(int y) const {
  const QRect contentRect = contentRectForWidget(rect());
  const QRect trackRect = trackRectForContent(contentRect);
  const QRect thumbRect = thumbRectForScrollBar(this, contentRect, trackRect);
  if (contentRect.isEmpty() || thumbRect.isEmpty())
    return minimum();

  const int rangeSpan = maximum() - minimum();
  if (rangeSpan <= 0)
    return minimum();

  const int contentTop = contentRect.top();
  const int localY =
      qBound(0, y - contentTop, qMax(0, contentRect.height() - 1));
  const int available = qMax(1, contentRect.height() - thumbRect.height());
  const qreal ratio = qBound<qreal>(
      0.0, static_cast<qreal>(localY - (thumbRect.height() / 2)) / available,
      1.0);

  return minimum() + qRound(ratio * rangeSpan);
}

void MiniMapWidget::scrollToY(int y) { setValue(scrollValueForY(y)); }

void MiniMapWidget::reconnectEditorSignals() {
  if (!m_editor)
    return;

  connect(m_editor, &QObject::destroyed, this, [this]() {
    m_editor = nullptr;
    update();
  });
  connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this,
          qOverload<>(&MiniMapWidget::update));
  connect(m_editor, &QPlainTextEdit::updateRequest, this,
          [this](const QRect &, int) { update(); });
  if (m_editor->document()) {
    connect(m_editor->document(), &QTextDocument::contentsChanged, this,
            qOverload<>(&MiniMapWidget::update));
  }
}

void MiniMapWidget::applyWidth() {
  const int width = m_overviewVisible ? m_overviewWidth : m_scrollBarWidth;
  setMinimumWidth(width);
  setMaximumWidth(width);
  updateGeometry();
}
