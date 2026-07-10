#include "CodeEditor/PerformanceMonitorWindow.h"
#include "CodeEditor/EditorTelemetry.h"
#include "CodeEditor/CodeEditor.h"
#include <QtWidgets>
#include <QHeaderView>
#include <QFileDialog>
#include <QTextStream>
#include <QDateTime>

#if defined(__linux__)
#include <malloc.h>
#endif

PerformanceMonitorWindow::PerformanceMonitorWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("QCodeEngine Performance Monitor"));
    resize(900, 650);
    setupUI();
    setupStyle();
    
    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(150); // 150ms updates
    connect(m_updateTimer, &QTimer::timeout, this, &PerformanceMonitorWindow::updateStats);
    m_updateTimer->start();

    // Connect to telemetry signals
    connect(EditorTelemetry::instance(), &EditorTelemetry::entryLogged, this, &PerformanceMonitorWindow::onEntryLogged);
    connect(EditorTelemetry::instance(), &EditorTelemetry::logCleared, this, &PerformanceMonitorWindow::filterLogs);

    // Initial load of logs
    filterLogs();
}

PerformanceMonitorWindow::~PerformanceMonitorWindow()
{
}

void PerformanceMonitorWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateStats();
    filterLogs();
}

void PerformanceMonitorWindow::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(15);

    // ── Header Title Area ───────────────────────────────────────────────────
    auto* headerLayout = new QHBoxLayout();
    auto* titleLabel = new QLabel(QStringLiteral("QCodeEngine Telemetry & Diagnostics"), this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold; color: #f5c2e7;"));
    
    auto* subtitleLabel = new QLabel(QStringLiteral("Real-time telemetry and instrumentation metrics"), this);
    subtitleLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #a6adc8; font-style: italic;"));
    
    auto* titleContainer = new QVBoxLayout();
    titleContainer->addWidget(titleLabel);
    titleContainer->addWidget(subtitleLabel);
    headerLayout->addLayout(titleContainer);
    headerLayout->addStretch();
    
    auto* trimBtn = new QPushButton(QStringLiteral("Trim Memory"), this);
    trimBtn->setToolTip(QStringLiteral("Reclaims unused process heap using malloc_trim"));
    connect(trimBtn, &QPushButton::clicked, this, &PerformanceMonitorWindow::triggerMemoryTrim);
    headerLayout->addWidget(trimBtn);
    
    mainLayout->addLayout(headerLayout);

    // ── Metrics Grid ────────────────────────────────────────────────────────
    auto* gridLayout = new QGridLayout();
    gridLayout->setSpacing(10);

    auto createCard = [this, gridLayout](const QString& label, const QString& initVal, int row, int col) {
        auto* card = new QFrame(this);
        card->setObjectName(QStringLiteral("MetricCard"));
        
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(4);
        
        auto* lbl = new QLabel(label, card);
        lbl->setObjectName(QStringLiteral("MetricLabel"));
        
        auto* val = new QLabel(initVal, card);
        val->setObjectName(QStringLiteral("MetricVal"));
        val->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        
        layout->addWidget(lbl);
        layout->addWidget(val);
        gridLayout->addWidget(card, row, col);
        return val;
    };

    m_fpsValLabel = createCard(QStringLiteral("REALTIME FPS"), QStringLiteral("0.0"), 0, 0);
    m_avgPaintValLabel = createCard(QStringLiteral("AVG PAINT TIME"), QStringLiteral("0.00 ms"), 0, 1);
    m_ramValLabel = createCard(QStringLiteral("RAM RSS"), QStringLiteral("0.0 MB"), 0, 2);
    m_vmValLabel = createCard(QStringLiteral("VIRTUAL SIZE"), QStringLiteral("0.0 MB"), 1, 0);
    m_docInfoLabel = createCard(QStringLiteral("DOCUMENT SIZE"), QStringLiteral("0 L / 0 C"), 1, 1);
    m_perfModeLabel = createCard(QStringLiteral("PERF CONFIG"), QStringLiteral("Normal Mode"), 1, 2);

    mainLayout->addLayout(gridLayout);

    // ── Filters & Controls ──────────────────────────────────────────────────
    auto* filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(10);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search details..."));
    connect(m_searchEdit, &QLineEdit::textChanged, this, &PerformanceMonitorWindow::filterLogs);
    filterLayout->addWidget(m_searchEdit, 2);

    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItem(QStringLiteral("All Operations"));
    m_filterCombo->addItem(QStringLiteral("Key Press"));
    m_filterCombo->addItem(QStringLiteral("Highlight"));
    m_filterCombo->addItem(QStringLiteral("Paint"));
    m_filterCombo->addItem(QStringLiteral("File Load"));
    connect(m_filterCombo, &QComboBox::currentTextChanged, this, &PerformanceMonitorWindow::filterLogs);
    filterLayout->addWidget(m_filterCombo, 1);

    m_slowOnlyCheck = new QCheckBox(QStringLiteral("Slow only (>2ms)"), this);
    m_slowOnlyCheck->setChecked(false);
    connect(m_slowOnlyCheck, &QCheckBox::toggled, this, &PerformanceMonitorWindow::filterLogs);
    filterLayout->addWidget(m_slowOnlyCheck);

    filterLayout->addSpacing(10);

    auto* exportBtn = new QPushButton(QStringLiteral("Export CSV"), this);
    connect(exportBtn, &QPushButton::clicked, this, &PerformanceMonitorWindow::exportCsv);
    filterLayout->addWidget(exportBtn);

    auto* clearBtn = new QPushButton(QStringLiteral("Clear Log"), this);
    connect(clearBtn, &QPushButton::clicked, this, &PerformanceMonitorWindow::clearLog);
    filterLayout->addWidget(clearBtn);

    mainLayout->addLayout(filterLayout);

    // ── Log History Table ───────────────────────────────────────────────────
    m_logTable = new QTableWidget(this);
    m_logTable->setColumnCount(5);
    m_logTable->setHorizontalHeaderLabels({
        QStringLiteral("Timestamp"),
        QStringLiteral("Operation"),
        QStringLiteral("Duration"),
        QStringLiteral("RAM Delta"),
        QStringLiteral("Details")
    });
    
    m_logTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_logTable->verticalHeader()->setVisible(false);
    m_logTable->setAlternatingRowColors(true);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    mainLayout->addWidget(m_logTable);
}

void PerformanceMonitorWindow::setupStyle()
{
    // Apply modern catppuccin dark styles
    setStyleSheet(QStringLiteral(
        "QWidget {"
        "    background-color: #1e1e2e;"
        "    color: #cdd6f4;"
        "    font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif;"
        "    font-size: 13px;"
        "}"
        "QFrame#MetricCard {"
        "    background-color: #181825;"
        "    border: 1px solid #313244;"
        "    border-radius: 8px;"
        "}"
        "QLabel#MetricLabel {"
        "    font-size: 10px;"
        "    font-weight: bold;"
        "    text-transform: uppercase;"
        "    color: #bac2de;"
        "    letter-spacing: 0.5px;"
        "}"
        "QLabel#MetricVal {"
        "    font-size: 22px;"
        "    font-weight: bold;"
        "    color: #89b4fa;"
        "}"
        "QTableWidget {"
        "    background-color: #11111b;"
        "    alternate-background-color: #181825;"
        "    border: 1px solid #313244;"
        "    border-radius: 6px;"
        "    gridline-color: #313244;"
        "    selection-background-color: #313244;"
        "    selection-color: #cdd6f4;"
        "}"
        "QHeaderView::section {"
        "    background-color: #181825;"
        "    color: #cdd6f4;"
        "    padding: 8px;"
        "    border: none;"
        "    border-bottom: 2px solid #313244;"
        "    font-weight: bold;"
        "}"
        "QPushButton {"
        "    background-color: #89b4fa;"
        "    color: #11111b;"
        "    border: none;"
        "    border-radius: 5px;"
        "    padding: 6px 12px;"
        "    font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "    background-color: #b4befe;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #74c7ec;"
        "}"
        "QLineEdit, QComboBox {"
        "    background-color: #181825;"
        "    border: 1px solid #313244;"
        "    border-radius: 5px;"
        "    padding: 5px 8px;"
        "    color: #cdd6f4;"
        "}"
        "QComboBox::drop-down {"
        "    border: none;"
        "}"
        "QCheckBox {"
        "    spacing: 5px;"
        "}"
        "QCheckBox::indicator {"
        "    width: 16px;"
        "    height: 16px;"
        "    border: 1px solid #313244;"
        "    border-radius: 3px;"
        "    background-color: #181825;"
        "}"
        "QCheckBox::indicator:checked {"
        "    background-color: #89b4fa;"
        "    image: url(:/qt-project.org/styles/commonstyle/images/checkmark-16.png);"
        "}"
        "QScrollBar:vertical {"
        "    border: none;"
        "    background: #11111b;"
        "    width: 10px;"
        "    margin: 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: #45475a;"
        "    min-height: 20px;"
        "    border-radius: 5px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background: #585b70;"
        "}"
    ));
}

void PerformanceMonitorWindow::updateStats()
{
    auto* telemetry = EditorTelemetry::instance();
    
    // FPS
    double fps = telemetry->currentFPS();
    m_fpsValLabel->setText(QString::number(fps, 'f', 1));
    if (fps >= 45.0) {
        m_fpsValLabel->setStyleSheet(QStringLiteral("color: #a6e3a1;")); // Green
    } else if (fps >= 20.0) {
        m_fpsValLabel->setStyleSheet(QStringLiteral("color: #f9e2af;")); // Yellow
    } else {
        m_fpsValLabel->setStyleSheet(QStringLiteral("color: #f38ba8;")); // Red
    }

    // Average Paint Time
    double avgPaint = telemetry->averagePaintTimeMs();
    m_avgPaintValLabel->setText(QStringLiteral("%1 ms").arg(avgPaint, 0, 'f', 2));
    if (avgPaint < 8.0) {
        m_avgPaintValLabel->setStyleSheet(QStringLiteral("color: #a6e3a1;"));
    } else if (avgPaint < 16.6) {
        m_avgPaintValLabel->setStyleSheet(QStringLiteral("color: #f9e2af;"));
    } else {
        m_avgPaintValLabel->setStyleSheet(QStringLiteral("color: #f38ba8;"));
    }

    // Memory usage
    double vm = 0.0, rss = 0.0;
    telemetry->getMemoryUsage(vm, rss);
    m_ramValLabel->setText(QStringLiteral("%1 MB").arg(rss, 0, 'f', 1));
    m_vmValLabel->setText(QStringLiteral("%1 MB").arg(vm, 0, 'f', 1));

    // Active Editor info
    CodeEditor* activeEditor = telemetry->activeEditor();
    if (activeEditor) {
        int lines = activeEditor->currentLine(); // fallback to query if needed, but we can query doc
        int totalLines = 0;
        int totalChars = 0;
        
        // Find document info
        QPlainTextEdit* textEdit = activeEditor->findChild<QPlainTextEdit*>();
        if (textEdit) {
            totalLines = textEdit->document()->blockCount();
            totalChars = textEdit->document()->characterCount();
        }

        m_docInfoLabel->setText(QStringLiteral("%1 L / %2 C").arg(totalLines).arg(totalChars));

        // Perf Mode Configuration
        QString modeText = QStringLiteral("Normal Mode");
        if (activeEditor->isLargeFileWindowedMode()) {
            modeText = QStringLiteral("Windowed Mode (Large File)");
            m_perfModeLabel->setStyleSheet(QStringLiteral("color: #f5c2e7;"));
        } else if (activeEditor->editableLargeFileMode()) {
            modeText = QStringLiteral("Large File (Suspended Features)");
            m_perfModeLabel->setStyleSheet(QStringLiteral("color: #f9e2af;"));
        } else {
            m_perfModeLabel->setStyleSheet(QStringLiteral("color: #a6e3a1;"));
        }
        
        m_perfModeLabel->setText(modeText);
    } else {
        m_docInfoLabel->setText(QStringLiteral("No Editor Active"));
        m_perfModeLabel->setText(QStringLiteral("Inactive"));
        m_perfModeLabel->setStyleSheet(QStringLiteral("color: #585b70;"));
    }
}

bool shouldShowEntryCheck(const TelemetryLogEntry& entry, const QString& filterText, const QString& searchText, bool slowOnly) {
    if (slowOnly && entry.durationMs <= 2.0 && entry.opType != QLatin1String("File Load")) {
        return false;
    }
    
    if (filterText != QLatin1String("All Operations")) {
        // match specific types
        if (filterText == QLatin1String("Key Press") && !entry.opType.contains(QLatin1String("Key"), Qt::CaseInsensitive)) {
            return false;
        }
        if (filterText == QLatin1String("Highlight") && !entry.opType.contains(QLatin1String("Highlight"), Qt::CaseInsensitive) && !entry.opType.contains(QLatin1String("Parse"), Qt::CaseInsensitive)) {
            return false;
        }
        if (filterText == QLatin1String("Paint") && !entry.opType.contains(QLatin1String("Paint"), Qt::CaseInsensitive)) {
            return false;
        }
        if (filterText == QLatin1String("File Load") && !entry.opType.contains(QLatin1String("Load"), Qt::CaseInsensitive)) {
            return false;
        }
    }

    if (!searchText.isEmpty()) {
        if (!entry.opType.contains(searchText, Qt::CaseInsensitive) &&
            !entry.details.contains(searchText, Qt::CaseInsensitive)) {
            return false;
        }
    }

    return true;
}

void PerformanceMonitorWindow::filterLogs()
{
    m_logTable->setUpdatesEnabled(false);
    m_logTable->clearContents();
    m_logTable->setRowCount(0);

    const QString filterText = m_filterCombo->currentText();
    const QString searchText = m_searchEdit->text().trimmed();
    const bool slowOnly = m_slowOnlyCheck->isChecked();

    const QList<TelemetryLogEntry> entries = EditorTelemetry::instance()->logEntries();

    QColor textColor("#cdd6f4");
    QColor warningColor("#f9e2af");
    QColor dangerColor("#f38ba8");
    QColor successColor("#a6e3a1");

    for (const auto& entry : entries) {
        if (!shouldShowEntryCheck(entry, filterText, searchText, slowOnly)) {
            continue;
        }

        int row = m_logTable->rowCount();
        m_logTable->insertRow(row);

        auto* itemTime = new QTableWidgetItem(entry.timestamp.toString(QStringLiteral("hh:mm:ss.zzz")));
        auto* itemOp = new QTableWidgetItem(entry.opType);
        auto* itemDur = new QTableWidgetItem(QStringLiteral("%1 ms").arg(entry.durationMs, 0, 'f', 2));
        
        QString memStr = QStringLiteral("0.0 MB");
        if (entry.memDiffMb != 0.0) {
            memStr = QStringLiteral("%1%2 MB").arg(entry.memDiffMb > 0 ? QStringLiteral("+") : QString()).arg(entry.memDiffMb, 0, 'f', 2);
        }
        auto* itemMem = new QTableWidgetItem(memStr);
        auto* itemDetails = new QTableWidgetItem(entry.details);

        itemTime->setTextAlignment(Qt::AlignCenter);
        itemOp->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        itemDur->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        itemMem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        itemDetails->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        if (entry.durationMs > 16.6) {
            itemDur->setForeground(dangerColor);
        } else if (entry.durationMs > 8.0) {
            itemDur->setForeground(warningColor);
        } else {
            itemDur->setForeground(successColor);
        }

        if (entry.memDiffMb > 5.0) {
            itemMem->setForeground(dangerColor);
        } else if (entry.memDiffMb < 0.0) {
            itemMem->setForeground(successColor);
        }

        m_logTable->setItem(row, 0, itemTime);
        m_logTable->setItem(row, 1, itemOp);
        m_logTable->setItem(row, 2, itemDur);
        m_logTable->setItem(row, 3, itemMem);
        m_logTable->setItem(row, 4, itemDetails);
    }

    m_logTable->setUpdatesEnabled(true);
    m_logTable->scrollToBottom();
}

void PerformanceMonitorWindow::onEntryLogged()
{
    const TelemetryLogEntry entry = EditorTelemetry::instance()->logEntries().last();
    const QString filterText = m_filterCombo->currentText();
    const QString searchText = m_searchEdit->text().trimmed();
    const bool slowOnly = m_slowOnlyCheck->isChecked();

    if (!shouldShowEntryCheck(entry, filterText, searchText, slowOnly)) {
        return;
    }

    m_logTable->setUpdatesEnabled(false);
    
    // Check if table row limit is exceeded
    if (m_logTable->rowCount() >= 2000) {
        m_logTable->removeRow(0);
    }

    int row = m_logTable->rowCount();
    m_logTable->insertRow(row);

    QColor warningColor("#f9e2af");
    QColor dangerColor("#f38ba8");
    QColor successColor("#a6e3a1");

    auto* itemTime = new QTableWidgetItem(entry.timestamp.toString(QStringLiteral("hh:mm:ss.zzz")));
    auto* itemOp = new QTableWidgetItem(entry.opType);
    auto* itemDur = new QTableWidgetItem(QStringLiteral("%1 ms").arg(entry.durationMs, 0, 'f', 2));
    
    QString memStr = QStringLiteral("0.0 MB");
    if (entry.memDiffMb != 0.0) {
        memStr = QStringLiteral("%1%2 MB").arg(entry.memDiffMb > 0 ? QStringLiteral("+") : QString()).arg(entry.memDiffMb, 0, 'f', 2);
    }
    auto* itemMem = new QTableWidgetItem(memStr);
    auto* itemDetails = new QTableWidgetItem(entry.details);

    itemTime->setTextAlignment(Qt::AlignCenter);
    itemOp->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    itemDur->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    itemMem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    itemDetails->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    if (entry.durationMs > 16.6) {
        itemDur->setForeground(dangerColor);
    } else if (entry.durationMs > 8.0) {
        itemDur->setForeground(warningColor);
    } else {
        itemDur->setForeground(successColor);
    }

    if (entry.memDiffMb > 5.0) {
        itemMem->setForeground(dangerColor);
    } else if (entry.memDiffMb < 0.0) {
        itemMem->setForeground(successColor);
    }

    m_logTable->setItem(row, 0, itemTime);
    m_logTable->setItem(row, 1, itemOp);
    m_logTable->setItem(row, 2, itemDur);
    m_logTable->setItem(row, 3, itemMem);
    m_logTable->setItem(row, 4, itemDetails);

    m_logTable->setUpdatesEnabled(true);
    m_logTable->scrollToBottom();
}

void PerformanceMonitorWindow::clearLog()
{
    EditorTelemetry::instance()->clearLog();
}

void PerformanceMonitorWindow::exportCsv()
{
    QString fileName = QFileDialog::getSaveFileName(this, QStringLiteral("Export Telemetry Log"), QString(), QStringLiteral("CSV Files (*.csv)"));
    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Export Error"), QStringLiteral("Could not write to file: %1").arg(fileName));
        return;
    }

    QTextStream out(&file);
    out << "Timestamp,Operation,Duration (ms),RAM Delta (MB),Details\n";

    const QList<TelemetryLogEntry> entries = EditorTelemetry::instance()->logEntries();
    for (const auto& entry : entries) {
        QString op = entry.opType;
        QString details = entry.details;
        out << entry.timestamp.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")) << ","
            << "\"" << op.replace(QStringLiteral("\""), QStringLiteral("\"\"")) << "\","
            << entry.durationMs << ","
            << entry.memDiffMb << ","
            << "\"" << details.replace(QStringLiteral("\""), QStringLiteral("\"\"")) << "\"\n";
    }

    file.close();
    QMessageBox::information(this, QStringLiteral("Export Complete"), QStringLiteral("Telemetry log exported successfully to %1").arg(fileName));
}

void PerformanceMonitorWindow::triggerMemoryTrim()
{
#if defined(__linux__)
    malloc_trim(0);
#endif
    updateStats();
}
