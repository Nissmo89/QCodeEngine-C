#pragma once

#include <QScrollBar>
#include <QPointer>
#include <QColor>

#include "CodeEditor/EditorTheme.h"

class QPlainTextEdit;
class QMouseEvent;
class QPaintEvent;
class DiagnosticManager;

class MiniMapWidget : public QScrollBar
{
    Q_OBJECT
public:
    explicit MiniMapWidget(QPlainTextEdit* editor, QWidget* parent = nullptr);

    void setEditor(QPlainTextEdit* editor);
    void setDiagnosticManager(DiagnosticManager* diagnosticManager);
    void setTheme(const QEditorTheme& theme);
    void setOverviewVisible(bool visible);
    bool overviewVisible() const { return m_overviewVisible; }
    void setMiniMapWidth(int width);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int scrollValueForY(int y) const;
    void scrollToY(int y);
    void reconnectEditorSignals();
    void applyWidth();

    QPointer<QPlainTextEdit> m_editor;
    QPointer<DiagnosticManager> m_diagnosticManager;
    QColor m_background;
    QColor m_borderColor;
    QColor m_trackColor;
    QColor m_thumbColor;
    QColor m_caretColor;
    QColor m_errorColor;
    QColor m_warningColor;
    int m_overviewWidth = 14;
    int m_scrollBarWidth = 12;
    bool m_overviewVisible = false;
    bool m_dragging = false;
};
