// ============================================================================
//  GutterWidget.cpp  –  QCodeEngine-C
// ============================================================================
#include "CodeEditor/GutterWidget.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QScrollBar>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QFontMetrics>
#include <QApplication>
#include <QDebug>
#include <QHash>

// ─────────────────────────────────────────────────────────────────────────────
//  Internal helpers  (file-scope, not exposed in header)
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Y-offset (viewport-relative) of the TOP of a text block
int blockTopY(QPlainTextEdit *ed, const QTextBlock &b)
{
    if (!b.isValid()) return -1;
    qreal top = ed->document()
                 ->documentLayout()
                 ->blockBoundingRect(b).top();
    top += ed->document()->documentMargin();
    return qRound(top - ed->verticalScrollBar()->value());
}

// Pixel height of one text block
int blockH(QPlainTextEdit *ed, const QTextBlock &b)
{
    if (!b.isValid()) return ed->fontMetrics().height();
    return qRound(ed->document()
                    ->documentLayout()
                    ->blockBoundingRect(b).height());
}

// Block whose bounding rect contains viewport-y  (invalid on miss)
QTextBlock blockAtViewportY(QPlainTextEdit *ed, int y)
{
    if (!ed) return {};
    // cursorForPosition clamps – safe even outside text area
    return ed->cursorForPosition(QPoint(1, qMax(0, y))).block();
}

// 1-based line number (block numbers are 0-based)
inline int lineNo(const QTextBlock &b)
{
    return b.isValid() ? b.blockNumber() + 1 : -1;
}

// ── Pixmap factories (no asset files required, all painted) ─────────────────
QPixmap makeCircle(const QColor &fill, int sz = 12)
{
    QPixmap px(sz, sz);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(fill);
    p.setPen(Qt::NoPen);
    p.drawEllipse(1, 1, sz - 2, sz - 2);
    return px;
}

QPixmap makeBookmarkPx(int sz = 12)
{
    QPixmap px(sz, sz);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(30, 130, 230));
    p.setPen(Qt::NoPen);
    QPainterPath path;
    path.moveTo(1, 0);
    path.lineTo(sz - 1, 0);
    path.lineTo(sz - 1, sz * 0.72);
    path.lineTo(sz / 2.0, sz * 0.55);
    path.lineTo(1, sz * 0.72);
    path.closeSubpath();
    p.drawPath(path);
    return px;
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
//  MarginArea
// ═════════════════════════════════════════════════════════════════════════════
MarginArea::MarginArea(QPlainTextEdit *editor, QWidget *parent)
    : QWidget(parent), m_ed(editor)
{
    setFixedWidth(WIDTH);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

// ── Marker management ────────────────────────────────────────────────────────
void MarginArea::setMarker(int line, MarkerType t)
{
    m_markers[line] |= t;
    update();
}
void MarginArea::clearMarker(int line, MarkerType t)
{
    if (!m_markers.contains(line)) return;
    m_markers[line] &= ~MarkerFlags(t);
    if (!m_markers[line]) m_markers.remove(line);
    update();
}
void MarginArea::toggleMarker(int line, MarkerType t)
{
    hasMarker(line, t) ? clearMarker(line, t) : setMarker(line, t);
}
void MarginArea::clearLine(int line)   { m_markers.remove(line); update(); }
bool MarginArea::hasMarker(int line, MarkerType t) const
{
    return m_markers.value(line).testFlag(t);
}
MarkerFlags MarginArea::markersAt(int line) const
{
    return m_markers.value(line);
}

// ── Paint ─────────────────────────────────────────────────────────────────────
void MarginArea::paintEvent(QPaintEvent *e)
{
    if (!m_ed) return;
    QPainter p(this);
    p.fillRect(e->rect(), palette().window());

    for (QTextBlock b = m_ed->document()->begin(); b.isValid(); b = b.next()) {
        int top = blockTopY(m_ed, b);
        int h   = blockH(m_ed, b);
        if (top + h < e->rect().top())  continue;
        if (top > e->rect().bottom())   break;

        MarkerFlags f = markersAt(lineNo(b));
        if (f) drawMarkers(p, QRect(1, top + 1, WIDTH - 2, h - 2), f);
    }
}

void MarginArea::drawMarkers(QPainter &p, const QRect &r, MarkerFlags f) const
{
    auto blit = [&](const QPixmap &pix) {
        if (pix.isNull()) return;
        p.drawPixmap(r, pix.scaled(r.size(), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
    };
    // Priority: Error > Warning > Breakpoint > Tracepoint > Bookmark > Info
    if (f.testFlag(MarkerType::Error))      { blit(px(MarkerType::Error));      return; }
    if (f.testFlag(MarkerType::Warning))    { blit(px(MarkerType::Warning));    return; }
    if (f.testFlag(MarkerType::Breakpoint)) { blit(px(MarkerType::Breakpoint)); return; }
    if (f.testFlag(MarkerType::Tracepoint)) { blit(px(MarkerType::Tracepoint)); return; }
    if (f.testFlag(MarkerType::Bookmark))   { blit(px(MarkerType::Bookmark));   return; }
    if (f.testFlag(MarkerType::Info))       { blit(px(MarkerType::Info));               }
}

const QPixmap &MarginArea::px(MarkerType t) const
{
    switch (t) {
    case MarkerType::Bookmark:
        if (m_pxBookmark.isNull())   m_pxBookmark   = makeBookmarkPx();
        return m_pxBookmark;
    case MarkerType::Breakpoint:
        if (m_pxBreakpoint.isNull()) m_pxBreakpoint = makeCircle(Qt::red);
        return m_pxBreakpoint;
    case MarkerType::Tracepoint:
        if (m_pxTracepoint.isNull()) m_pxTracepoint = makeCircle(QColor(220,160,0));
        return m_pxTracepoint;
    case MarkerType::Warning:
        if (m_pxWarning.isNull())    m_pxWarning    = makeCircle(QColor(255,200,0));
        return m_pxWarning;
    case MarkerType::Error:
        if (m_pxError.isNull())      m_pxError      = makeCircle(QColor(200,0,0));
        return m_pxError;
    case MarkerType::Info:
        if (m_pxInfo.isNull())       m_pxInfo       = makeCircle(QColor(0,180,200));
        return m_pxInfo;
    default:
        static QPixmap empty;
        return empty;
    }
}

// ── Mouse ─────────────────────────────────────────────────────────────────────
void MarginArea::mousePressEvent(QMouseEvent *e)
{
    if (!m_ed || e->button() != Qt::LeftButton) return;
    int ln = lineNo(blockAtViewportY(m_ed, e->pos().y()));
    if (ln < 0) return;
    toggleMarker(ln, MarkerType::Bookmark);
    emit markerToggled(ln, MarkerType::Bookmark);
}

void MarginArea::contextMenuEvent(QContextMenuEvent *e)
{
    if (!m_ed) return;
    int ln = lineNo(blockAtViewportY(m_ed, e->pos().y()));
    if (ln < 0) return;

    QMenu menu(this);
    auto addItem = [&](const QString &label, MarkerType t) {
        auto *a = menu.addAction(label);
        a->setCheckable(true);
        a->setChecked(hasMarker(ln, t));
        connect(a, &QAction::triggered, this, [=] {
            toggleMarker(ln, t);
            emit markerToggled(ln, t);
        });
    };
    addItem(tr("Bookmark"),   MarkerType::Bookmark);
    addItem(tr("Breakpoint"), MarkerType::Breakpoint);
    addItem(tr("Tracepoint"), MarkerType::Tracepoint);
    menu.addSeparator();
    menu.addAction(tr("Clear all markers on line %1").arg(ln), this,
                   [=] { clearLine(ln); });
    menu.exec(e->globalPos());
}

// ═════════════════════════════════════════════════════════════════════════════
//  LineNumberArea
// ═════════════════════════════════════════════════════════════════════════════
LineNumberArea::LineNumberArea(QPlainTextEdit *editor, QWidget *parent)
    : QWidget(parent), m_ed(editor)
{
    setFixedWidth(preferredWidth());
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

int LineNumberArea::preferredWidth() const
{
    if (!m_ed) return 30;
    int digits = 1;
    int mx = qMax(1, m_ed->document()->blockCount());
    while (mx >= 10) { mx /= 10; ++digits; }
    return 2 * PAD + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void LineNumberArea::paintEvent(QPaintEvent *e)
{
    if (!m_ed) return;

    // Auto-resize if digit count changed (GutterWidget will pick this up)
    int pw = preferredWidth();
    if (pw != width()) setFixedWidth(pw);

    QPainter p(this);
    p.fillRect(e->rect(), palette().window());

    QFont baseFont = m_ed->font();

    for (QTextBlock b = m_ed->document()->begin(); b.isValid(); b = b.next()) {
        int top = blockTopY(m_ed, b);
        int h   = blockH(m_ed, b);
        int ln  = lineNo(b);

        if (top + h < e->rect().top())  continue;
        if (top > e->rect().bottom())   break;

        if (ln == m_curLine) {
            // Current line: bold + highlight colour
            QFont bold = baseFont;
            bold.setBold(true);
            p.setFont(bold);
            p.setPen(palette().color(QPalette::Highlight));
        } else {
            p.setFont(baseFont);
            p.setPen(palette().color(QPalette::Text));
        }

        p.drawText(0, top, width() - PAD / 2, h,
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(ln));
    }
}

void LineNumberArea::mousePressEvent(QMouseEvent *e)
{
    if (!m_ed) return;
    QTextBlock b = blockAtViewportY(m_ed, e->pos().y());
    if (!b.isValid()) return;
    QTextCursor cur(b);
    cur.select(QTextCursor::LineUnderCursor);
    m_ed->setTextCursor(cur);
}

// ═════════════════════════════════════════════════════════════════════════════
//  FoldArea
// ═════════════════════════════════════════════════════════════════════════════
FoldArea::FoldArea(QPlainTextEdit *editor, QWidget *parent)
    : QWidget(parent), m_ed(editor)
{
    setFixedWidth(WIDTH);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

void FoldArea::setRanges(const QList<FoldRange> &r) { m_ranges = r; update(); }

void FoldArea::toggle(int startLine)
{
    FoldRange *r = rangeAt(startLine);
    if (!r) return;
    r->folded = !r->folded;
    emit foldToggled(startLine, r->folded);
    update();
}

bool FoldArea::isFolded(int startLine) const
{
    for (const FoldRange &r : m_ranges)
        if (r.startLine == startLine) return r.folded;
    return false;
}

void FoldArea::paintEvent(QPaintEvent *e)
{
    if (!m_ed) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(e->rect(), palette().window());

    // Build fast lookup: startLine → index
    QHash<int, int> idx;
    for (int i = 0; i < m_ranges.size(); ++i)
        idx[m_ranges[i].startLine] = i;

    for (QTextBlock b = m_ed->document()->begin(); b.isValid(); b = b.next()) {
        int top = blockTopY(m_ed, b);
        int h   = blockH(m_ed, b);
        int ln  = lineNo(b);

        if (top + h < e->rect().top())  continue;
        if (top > e->rect().bottom())   break;

        if (!idx.contains(ln)) continue;

        const FoldRange &fr = m_ranges[idx[ln]];
        int side = qMin(h - 2, WIDTH - 2);
        QRect arrowR((WIDTH - side) / 2, top + (h - side) / 2, side, side);
        drawArrow(p, arrowR, fr.folded, ln == m_hovered);
    }
}

void FoldArea::drawArrow(QPainter &p, const QRect &r,
                         bool folded, bool hovered) const
{
    QColor base = palette().color(QPalette::Text);
    QColor col = hovered ? palette().color(QPalette::Highlight) : base;
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(col);

    QPointF c  = r.center();
    double  sz = r.width() * 0.4;
    QPainterPath path;
    if (folded) {
        // ▶ right-pointing
        path.moveTo(c.x() - sz * 0.5, c.y() - sz);
        path.lineTo(c.x() + sz,       c.y());
        path.lineTo(c.x() - sz * 0.5, c.y() + sz);
    } else {
        // ▼ down-pointing
        path.moveTo(c.x() - sz,       c.y() - sz * 0.5);
        path.lineTo(c.x() + sz,       c.y() - sz * 0.5);
        path.lineTo(c.x(),            c.y() + sz);
    }
    path.closeSubpath();
    p.drawPath(path);
    p.restore();
}

void FoldArea::mousePressEvent(QMouseEvent *e)
{
    if (!m_ed || e->button() != Qt::LeftButton) return;
    int ln = lineNo(blockAtViewportY(m_ed, e->pos().y()));
    if (ln >= 0) toggle(ln);
}

void FoldArea::mouseMoveEvent(QMouseEvent *e)
{
    int ln = lineNo(blockAtViewportY(m_ed, e->pos().y()));
    if (ln != m_hovered) { m_hovered = ln; update(); }
}

void FoldArea::leaveEvent(QEvent *)
{
    if (m_hovered != -1) { m_hovered = -1; update(); }
}

FoldArea::FoldRange *FoldArea::rangeAt(int line)
{
    for (FoldRange &r : m_ranges)
        if (r.startLine == line) return &r;
    return nullptr;
}

// ═════════════════════════════════════════════════════════════════════════════
//  GutterWidget
// ═════════════════════════════════════════════════════════════════════════════
GutterWidget::GutterWidget(QPlainTextEdit *editor, QWidget *parent)
    : QWidget(parent)
    , m_ed(editor)
    , m_margin (new MarginArea    (editor, this))
    , m_lineNum (new LineNumberArea(editor, this))
    , m_fold    (new FoldArea      (editor, this))
{
    Q_ASSERT_X(editor, "GutterWidget", "editor must not be null");

    // Null-safe fallback: if editor is destroyed before us, stop painting
    connect(editor, &QObject::destroyed, this, [this] { m_ed = nullptr; });

    // Forward inner signals to GutterWidget's own signals
    connect(m_margin, &MarginArea::markerToggled,
            this, &GutterWidget::markerToggled);
    connect(m_fold,   &FoldArea::foldToggled,
            this,     &GutterWidget::foldToggled);

    connectEditor();
    relayout();
}

int GutterWidget::totalWidth() const
{
    return MarginArea::WIDTH
         + m_lineNum->preferredWidth()
         + FoldArea::WIDTH;
}

void GutterWidget::relayout()
{
    // Manual layout – avoids QHBoxLayout overhead and keeps us in full control
    int lnW = m_lineNum->isVisible() ? m_lineNum->preferredWidth() : 0;
    int h   = height();
    int foldW = m_fold->isVisible() ? FoldArea::WIDTH : 0;

    m_margin ->setGeometry(0,
                           0,
                           MarginArea::WIDTH,
                           h);
    m_lineNum->setGeometry(MarginArea::WIDTH,
                           0,
                           lnW,
                           h);
    m_fold   ->setGeometry(MarginArea::WIDTH + lnW,
                           0,
                           foldW,
                           h);
}

void GutterWidget::updateWidth()
{
    if (!m_ed) return;
    int needed = totalWidth();

    // Update LineNumberArea's fixed width first (it may have grown)
    int lnW = m_lineNum->isVisible() ? m_lineNum->preferredWidth() : 0;
    m_lineNum->setFixedWidth(lnW);
    relayout();

    if (needed != width()) {
        setFixedWidth(needed);
        // When used inside a layout, the parent widget keeps the gutter positioned.
    }

    update();
}

void GutterWidget::setTheme(const QEditorTheme &theme)
{
    Q_UNUSED(theme)
    if (!m_ed) return;
    QPalette pal = m_ed->palette();
    setPalette(pal);
    m_margin->setPalette(pal);
    m_lineNum->setPalette(pal);
    m_fold->setPalette(pal);
    update();
}

void GutterWidget::setLineNumbersVisible(bool visible)
{
    m_lineNum->setVisible(visible);
    updateWidth();
}

void GutterWidget::setFoldingVisible(bool visible)
{
    m_fold->setVisible(visible);
    updateWidth();
}

static MarkerType markerTypeForIcon(GutterIconType type)
{
    switch (type) {
    case GutterIconType::Error:      return MarkerType::Error;
    case GutterIconType::Warning:    return MarkerType::Warning;
    case GutterIconType::Info:       return MarkerType::Info;
    case GutterIconType::Tip:        return MarkerType::Info;
    case GutterIconType::Breakpoint: return MarkerType::Breakpoint;
    default:                         return MarkerType::Info;
    }
}

void GutterWidget::setFoldRanges(const QList<FoldArea::FoldRange> &ranges)
{
    m_fold->setRanges(ranges);
}

void GutterWidget::setCurrentLine(int line)
{
    m_lineNum->setCurrentLine(line);
}

void GutterWidget::setIconMap(const QMap<int, GutterIconInfo> &icons)
{
    for (auto it = m_icons.begin(); it != m_icons.end(); ++it) {
        m_margin->clearLine(it.key());
    }
    m_icons = icons;
    for (auto it = m_icons.begin(); it != m_icons.end(); ++it) {
        m_margin->setMarker(it.key(), markerTypeForIcon(it.value().type));
    }
    update();
}

void GutterWidget::syncScrollWith(const QRect &rect, int dy)
{
    if (!m_ed) return;

    if (dy) {
        // Fast scroll path – bitblit, no full repaint
        m_margin ->scroll(0, dy);
        m_lineNum->scroll(0, dy);
        m_fold   ->scroll(0, dy);
    } else {
        m_margin ->update(0, rect.y(), m_margin ->width(), rect.height());
        m_lineNum->update(0, rect.y(), m_lineNum->width(), rect.height());
        m_fold   ->update(0, rect.y(), m_fold   ->width(), rect.height());
    }

    // Check if line-number width needs to change (e.g. crossed 1000-line mark)
    if (rect.contains(m_ed->viewport()->rect()))
        updateWidth();
}

void GutterWidget::connectEditor()
{
    if (!m_ed) return;

    // Widen gutter when block count crosses a digit boundary
    connect(m_ed->document(), &QTextDocument::blockCountChanged,
            this, [this](int) { updateWidth(); });

    // Scroll synchronisation
    connect(m_ed, &QPlainTextEdit::updateRequest,
            this, [this](const QRect &r, int dy) { syncScrollWith(r, dy); });

    // Highlight current line number
    connect(m_ed, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (m_ed)
            m_lineNum->setCurrentLine(m_ed->textCursor().blockNumber() + 1);
    });

    // Initial sizing
    int needed = totalWidth();
    setFixedWidth(needed);
}

void GutterWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    relayout();
}
