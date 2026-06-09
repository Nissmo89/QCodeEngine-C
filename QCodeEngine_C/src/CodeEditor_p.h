#pragma once
#include <QPlainTextEdit>
#include <QMap>
#include <QTimer>
#include <QThread>
#include "CodeEditor/CodeEditor.h"
#include "CodeEditor/GutterWidget.h"
#include "CodeEditor/diagnosticmanager.h"
#include "FoldManager.h"
#include <QTextBlock>
#include "TreeSitterHighlighter.h"
#include "AutoCompleter.h"
#include "syntaxerrordetector.h"
#include "treesitterhelper.h"
#include "CodeEditor/LineHighlighter.h"
#include "CodeEditor/FindReplaceBar.h"
#include "CodeEditor/MiniMapWidget.h"

class QKeyEvent;
class QMouseEvent;
class QFocusEvent;
class QEvent;
class LiveIndentController;

// Forward declarations for future stages
// class QSyntaxHighlighter_TS;
// class QAutoCompleter;
// class QMiniMap;
// class QSearchBar;

class CodeEditorPrivate;
struct LargeFileState;
struct MultiCursorState {
    int anchor = 0;
    int position = 0;
};

struct BracketGuideState {
    int startBlock = -1;
    int endBlock = -1;
    int openColumn = -1;
    int closeColumn = -1;
    int depth = 0;
    QString prefix;

    bool isValid() const
    {
        return startBlock >= 0
               && endBlock > startBlock
               && openColumn >= 0
               && closeColumn >= 0;
    }

    void clear()
    {
        startBlock = -1;
        endBlock = -1;
        openColumn = -1;
        closeColumn = -1;
        depth = 0;
        prefix.clear();
    }
};

class InnerEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit InnerEditor(CodeEditorPrivate* d, QWidget* parent = nullptr);

    QTextBlock firstVisibleBlock() const { return QPlainTextEdit::firstVisibleBlock(); }
    QRectF blockBoundingGeometry(const QTextBlock &block) const { return QPlainTextEdit::blockBoundingGeometry(block); }
    QPointF contentOffset() const { return QPlainTextEdit::contentOffset(); }
    QRectF blockBoundingRect(const QTextBlock &block) const { return QPlainTextEdit::blockBoundingRect(block); }

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    bool foldedChipAt(const QPoint& pos, int* foldStart, QRect* chipRect, int* hiddenLines) const;

    CodeEditorPrivate* d_ptr;
    int m_hoveredFoldStart = -1;
};

class CodeEditorPrivate : public QObject {
    Q_OBJECT
public:
    explicit CodeEditorPrivate(CodeEditor* q, QWidget* parent = nullptr);
    ~CodeEditorPrivate() override;



    void updateCurrentLineHighlight();
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect& rect, int dy);
    void updateGutterFoldRanges();          // sync gutter arrows from FoldManager
    void setFoldingEnabled(bool enabled);   // enable/disable fold feature

    bool handleKeyPress(QKeyEvent* e);
    void updateBracketMatch();
    void indentSelection(bool indent);
    void toggleLineComment();

    CodeEditor* q_ptr;
    InnerEditor* m_editor;
    // GutterWidget* m_gutter;
    FoldManager* m_foldManager;

    QEditorTheme m_theme;
    QMap<int, GutterIconInfo> m_icons;

    // Feature toggles
    bool m_insertSpaces    = true;
    int  m_tabWidth        = 4;
    bool m_autoBracket     = true;
    bool m_autoIndent      = true;
    IndentStylePreset m_indentStylePreset = IndentStylePreset::KR;
    bool m_foldingEnabled  = false;
    bool m_bracketPairGuidesEnabled = false;
    bool m_miniMapVisibleRequested = false;
    bool m_largeFileMode = false;
    bool m_largeDocumentMode = false;
    bool m_preferEditableLargeFiles = false;
    bool m_savedReadOnly = false;
    bool m_asyncLoadInProgress = false;
    bool m_heavyFeaturesSuspended = false;

    QString m_lastSearchTerm;
    bool m_lastSearchCaseSensitive = false;
    bool m_lastSearchRegex = false;
    QString m_asyncLoadedText;
    QString m_asyncLoadedPath;
    QString m_loadedFilePath;
    qint64 m_asyncLoadedBytes = 0;
    qint64 m_loadedFileSize = 0;
    qsizetype m_asyncLoadOffset = 0;
    int m_asyncLoadGeneration = 0;

    QTimer* m_functionListTimer = nullptr;  // debounces updateFunctionList
    QTimer* m_largeDocHighlightTimer = nullptr;
    int m_pendingLargeDocHighlightLine = -1;

    QList<QTextEdit::ExtraSelection> m_bracketSelections;
    QList<QTextEdit::ExtraSelection> m_searchSelections;
    QList<QTextEdit::ExtraSelection> m_lineHighlightSelections;
    QVector<MultiCursorState> m_multiCursors;
    BracketGuideState m_activeBracketGuide;
    QMap<QString, CodeEditorPlugin*> m_plugins;

    // Future components
    TreeSitterHighlighter* m_highlighter    = nullptr;
    LineHighlighter*       m_lineHighlighter = nullptr;
    AutoCompleter* m_completer = nullptr;
    MiniMapWidget* m_miniMap = nullptr;
    FindReplaceBar* m_searchBar = nullptr;
    GutterWidget *m_gutter = nullptr;
    LiveIndentController* m_liveIndentController = nullptr;

    // ✅ NEW members
    FloatingListPopup *m_functionPopup = nullptr;
    TreeSitterHelper *m_treeSitterHelper = nullptr;

    DiagnosticManager *m_diagnosticManager;
    SyntaxErrorDetector *m_syntaxChecker;
    LargeFileState* m_largeFileState = nullptr;
    QThread* m_asyncLoadThread = nullptr;
    int m_zoomPointSize = -1;



    // ✅ NEW functions
    void updateFunctionList();
    void onFunctionSelected(int line);


    void setupLayout();
    void setupHighlighter();
    void setupEditorModules();
    void setupConnections();
    void setupActions();
    void adjustZoom(int delta);
    void onTreeParsed(void* treePtr);
    void updateActiveBracketGuide(bool forceRepaint = false);
    void syncMiniMapVisibility();
    void enforceFixedLineHeight(int from, int charsAdded);
    void onGutterMarkerToggled(int line, MarkerType type);
    bool dispatchPluginKeyPress(QKeyEvent* event);
    void detachPlugins();
    bool addExtraCursorAtPosition(int position, bool toggleExisting = true);
    void clearExtraCursors();
    void normalizeExtraCursors();
    bool moveExtraCursorsVertically(int deltaBlocks);
    bool handleMultiCursorEdit(QKeyEvent* event);
    QList<QTextEdit::ExtraSelection> multiCursorSelections() const;
    bool shouldUseLargeFileMode(qint64 fileSize) const;
    bool shouldUseLargeDocumentMode(qint64 sourceBytesHint) const;
    void applyDocumentPerformanceMode(qint64 sourceBytesHint = -1);
    bool enterLargeFileMode(const QString& filePath);
    void exitLargeFileMode();
    void suspendHeavyEditorFeatures();
    void resumeHeavyEditorFeatures();
    void requestLargeFileWindow(qint64 requestedByte, int anchorMode);
    void applyLargeFileWindow(int requestId, qint64 startByte, qint64 endByte,
                              const QString& text, int anchorMode);
    void onLargeFileScroll(int value);
    void startLargeFileIndexing();
    int largeFileCurrentLine() const;
    qint64 largeFileByteForLine(int line) const;
    bool shouldUseAsyncFullLoad(qint64 fileSize) const;
    bool startAsyncFileLoad(const QString& filePath);
    void cancelAsyncFileLoad();
    void beginChunkedTextApply(QString text, const QString& filePath);
    void applyNextTextChunk(int generation);








private:

public slots:
    void onCursorPositionChanged();
    void onTextChanged();
    void onGutterFoldClicked(int line, bool folded);
};
