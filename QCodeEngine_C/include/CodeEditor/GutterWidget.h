#pragma once
#include <QWidget>
#include <QMap>
#include <QSet>
// Removed QPlainTextEdit here, using forward declaration for InnerEditor
#include "CodeEditor/EditorTheme.h"
#include "CodeEditor/EditorTypes.h"

class FoldManager;
class InnerEditor;

class GutterWidget : public QWidget {
    Q_OBJECT
public:
    explicit GutterWidget(QWidget* parent = nullptr);
    int requiredWidth() const;
    void setEditor(InnerEditor* editor);
    void setTheme(const QEditorTheme& theme);
    void setIconMap(const QMap<int, GutterIconInfo>& icons);
    void setFoldManager(FoldManager* foldManager);
    void setLineNumbersVisible(bool v);
    void setFoldingVisible(bool v);

    // Active line (set by editor, 0-based block number)
    void setCurrentBlock(int blockNumber);

signals:
    void foldClicked(int blockNumber);
    void iconClicked(int blockNumber);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int lineNumberWidth() const;
    void drawIcon(QPainter& painter, const GutterIconInfo& info, int x, int y, int h);
    void drawFoldArrow(QPainter& painter, bool folded, int x, int y, int h);
    int iconX() const;
    int foldArrowX() const;

    InnerEditor*             m_editor      = nullptr;
    QEditorTheme             m_theme;
    QMap<int, GutterIconInfo> m_icons;
    FoldManager*            m_foldManager = nullptr;
    bool m_showLineNumbers = true;
    bool m_showFolding     = true;

    QMap<int, QRect> m_foldArrowRects;
    QMap<int, QRect> m_iconRects;
    QMap<int, QRect> m_lineNumberRects;
    int iconSize  = 12;
    int arrowSize = 14;

    // Hover / active state
    int m_hoveredBlock  = -1; // block number under mouse (-1 = none)
    int m_currentBlock  = -1; // active cursor line
};
