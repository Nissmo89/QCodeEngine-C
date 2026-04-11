#pragma once
// ============================================================================
//  GutterWidget.h  –  QCodeEngine-C
//  3-panel gutter: [MarginArea | LineNumbers | FoldArea]
//
//  Drop this file + GutterWidget.cpp into QCodeEngine_C/ and add them to
//  CMakeLists.txt under the QCodeEngine_C target.  Then in CodeEditor.cpp
//  replace the old single-widget gutter construction with GutterWidget.
// ============================================================================

#include <QWidget>
#include <QPlainTextEdit>
#include <QMap>
#include <QList>
#include <QPixmap>
#include "EditorTheme.h"
#include "EditorTypes.h"

QT_BEGIN_NAMESPACE
class QPaintEvent;
class QMouseEvent;
class QContextMenuEvent;
class QEvent;
class QResizeEvent;
QT_END_NAMESPACE

// ─────────────────────────────────────────────────────────────────────────────
//  MarkerType  ·  what can live in the left margin
// ─────────────────────────────────────────────────────────────────────────────
enum class MarkerType : quint8 {
    None       = 0,
    Bookmark   = 1 << 0,
    Breakpoint = 1 << 1,
    Tracepoint = 1 << 2,
    Warning    = 1 << 3,
    Error      = 1 << 4,
    Info       = 1 << 5,
};
Q_DECLARE_FLAGS(MarkerFlags, MarkerType)
Q_DECLARE_OPERATORS_FOR_FLAGS(MarkerFlags)

// ─────────────────────────────────────────────────────────────────────────────
//  MarginArea  ·  leftmost panel  (bookmark / breakpoint / tracepoint icons)
// ─────────────────────────────────────────────────────────────────────────────
class MarginArea : public QWidget
{
    Q_OBJECT
public:
    static constexpr int WIDTH = 18;

    explicit MarginArea(QPlainTextEdit *editor, QWidget *parent = nullptr);

    void       setMarker   (int line, MarkerType t);
    void       clearMarker (int line, MarkerType t);
    void       toggleMarker(int line, MarkerType t);
    void       clearLine   (int line);
    bool       hasMarker   (int line, MarkerType t) const;
    MarkerFlags markersAt  (int line) const;

signals:
    void markerToggled(int line, MarkerType type);

protected:
    void paintEvent       (QPaintEvent   *e) override;
    void mousePressEvent  (QMouseEvent   *e) override;
    void contextMenuEvent (QContextMenuEvent *e) override;

private:
    QPlainTextEdit          *m_ed;
    QMap<int, MarkerFlags>   m_markers;   // 1-based line → flags

    // Lazy-initialised, painted-from-scratch pixmaps (no asset files needed)
    mutable QPixmap m_pxBookmark, m_pxBreakpoint, m_pxTracepoint,
                    m_pxWarning,  m_pxError,       m_pxInfo;

    void        drawMarkers(QPainter &p, const QRect &r, MarkerFlags f) const;
    const QPixmap &px(MarkerType t) const;
};

// ─────────────────────────────────────────────────────────────────────────────
//  LineNumberArea  ·  middle panel  (line numbers + current-line highlight)
// ─────────────────────────────────────────────────────────────────────────────
class LineNumberArea : public QWidget
{
    Q_OBJECT
public:
    explicit LineNumberArea(QPlainTextEdit *editor, QWidget *parent = nullptr);

    int  preferredWidth() const;
    void setCurrentLine(int line) { m_curLine = line; update(); }

protected:
    void paintEvent     (QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;   // click = select line

private:
    QPlainTextEdit *m_ed;
    int             m_curLine = -1;

    static constexpr int PAD = 6;   // px padding each side
};

// ─────────────────────────────────────────────────────────────────────────────
//  FoldArea  ·  rightmost panel  (▶ / ▼ fold arrows with hover)
// ─────────────────────────────────────────────────────────────────────────────
class FoldArea : public QWidget
{
    Q_OBJECT
public:
    static constexpr int WIDTH = 14;

    struct FoldRange {
        int  startLine = -1;
        int  endLine   = -1;
        bool folded    = false;
    };

    explicit FoldArea(QPlainTextEdit *editor, QWidget *parent = nullptr);

    void setRanges (const QList<FoldRange> &ranges);
    void toggle    (int startLine);
    bool isFolded  (int startLine) const;

signals:
    void foldToggled(int startLine, bool folded);

protected:
    void paintEvent     (QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent (QMouseEvent *e) override;
    void leaveEvent     (QEvent      *e) override;

private:
    QPlainTextEdit  *m_ed;
    QList<FoldRange> m_ranges;
    int              m_hovered = -1;

    FoldRange *rangeAt(int line);
    void       drawArrow(QPainter &p, const QRect &r,
                         bool folded, bool hovered) const;
};

// ─────────────────────────────────────────────────────────────────────────────
//  GutterWidget  ·  container that owns all three panels
//
//  Layout:  [MarginArea | LineNumberArea | FoldArea]
//
//  Usage inside CodeEditor:
//
//    m_gutter = new GutterWidget(this);
//    // in resizeEvent / updateRequest:
//    m_gutter->syncScrollWith(rect, dy);
//    // apply width to editor viewport:
//    setViewportMargins(m_gutter->totalWidth(), 0, 0, 0);
//    m_gutter->setGeometry(0, 0, m_gutter->totalWidth(), height());
// ─────────────────────────────────────────────────────────────────────────────
class GutterWidget : public QWidget
{
    Q_OBJECT
public:
    explicit GutterWidget(QPlainTextEdit *editor, QWidget *parent = nullptr);

    // Panel accessors
    MarginArea     *margin()     const { return m_margin; }
    LineNumberArea *lineNumbers()const { return m_lineNum; }
    FoldArea       *fold()       const { return m_fold;   }

    // Total pixel width — use for setViewportMargins + setGeometry
    int totalWidth() const;

    void setTheme(const QEditorTheme &theme);
    void setLineNumbersVisible(bool visible);
    void setFoldingVisible(bool visible);
    void setFoldRanges(const QList<FoldArea::FoldRange> &ranges);
    void setCurrentLine(int line);
    void setIconMap(const QMap<int, GutterIconInfo> &icons);

    // Call from CodeEditor's updateRequest slot
    void syncScrollWith(const QRect &rect, int dy);

    // Call from CodeEditor's blockCountChanged slot
    void updateWidth();

protected:
    void resizeEvent(QResizeEvent *e) override;

signals:
    void markerToggled(int line, MarkerType type);
    void foldToggled  (int line, bool folded);

private:
    QPlainTextEdit *m_ed;
    MarginArea     *m_margin;
    LineNumberArea *m_lineNum;
    FoldArea       *m_fold;
    QMap<int, GutterIconInfo> m_icons;

    void connectEditor();
    void relayout();    // repositions the 3 children
};
