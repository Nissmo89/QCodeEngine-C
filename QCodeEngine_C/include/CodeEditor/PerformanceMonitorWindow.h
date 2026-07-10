#pragma once

#include <QWidget>
#include <QTimer>

class QTableWidget;
class QLabel;
class QComboBox;
class QLineEdit;
class QCheckBox;

class PerformanceMonitorWindow : public QWidget {
    Q_OBJECT
public:
    explicit PerformanceMonitorWindow(QWidget* parent = nullptr);
    ~PerformanceMonitorWindow() override;

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void updateStats();
    void onEntryLogged();
    void filterLogs();
    void clearLog();
    void exportCsv();
    void triggerMemoryTrim();

private:
    void setupUI();
    void setupStyle();

    QLabel* m_fpsValLabel = nullptr;
    QLabel* m_avgPaintValLabel = nullptr;
    QLabel* m_ramValLabel = nullptr;
    QLabel* m_vmValLabel = nullptr;
    QLabel* m_docInfoLabel = nullptr;
    QLabel* m_perfModeLabel = nullptr;

    QLineEdit* m_searchEdit = nullptr;
    QComboBox* m_filterCombo = nullptr;
    QCheckBox* m_slowOnlyCheck = nullptr;
    QTableWidget* m_logTable = nullptr;

    QTimer* m_updateTimer = nullptr;
};
