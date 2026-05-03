#pragma once

#include <QWidget>
#include <QPointer>
#include <QColor>

#include "CodeEditor/EditorTheme.h"

class QPlainTextEdit;
class QMouseEvent;
class QPaintEvent;

class MiniMapWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MiniMapWidget(QPlainTextEdit* editor, QWidget* parent = nullptr);

    void setEditor(QPlainTextEdit* editor);
    void setTheme(const QEditorTheme& theme);
    void setMiniMapWidth(int width);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int lineForY(int y) const;
    void navigateToY(int y);
    void reconnectEditorSignals();

    QPointer<QPlainTextEdit> m_editor;
    QColor m_background;
    QColor m_lineColor;
    QColor m_viewportFill;
    QColor m_viewportBorder;
    int m_width = 110;
    bool m_dragging = false;
};
