#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QQueue>
#include <QMutex>
#include <QList>

class CodeEditor;

struct TelemetryLogEntry {
    QDateTime timestamp;
    QString opType;
    QString details;
    double durationMs;
    double memBeforeMb;
    double memAfterMb;
    double memDiffMb;
};

class EditorTelemetry : public QObject {
    Q_OBJECT
public:
    static EditorTelemetry* instance();

    void registerEditor(CodeEditor* editor);
    void unregisterEditor(CodeEditor* editor);
    CodeEditor* activeEditor() const;

    // Recording operations
    void recordOperation(const QString& opType, const QString& details, double durationMs);
    void recordPaint(qint64 durationNs, double durationMs);
    void startLoadFile(const QString& filePath);
    void endLoadFile(const QString& filePath, qint64 fileSize, int lines, int chars);

    // Stats queries
    double currentFPS() const;
    double averagePaintTimeMs() const;
    void getMemoryUsage(double& vmUsageMb, double& rssUsageMb) const;
    QList<TelemetryLogEntry> logEntries() const;
    void clearLog();

signals:
    void entryLogged(const TelemetryLogEntry& entry);
    void statsUpdated();
    void logCleared();

private:
    explicit EditorTelemetry(QObject* parent = nullptr);
    ~EditorTelemetry() override;

    mutable QMutex m_mutex;
    CodeEditor* m_activeEditor = nullptr;
    QList<TelemetryLogEntry> m_logEntries;
    
    QQueue<qint64> m_paintTimestamps;
    QQueue<double> m_paintDurations;

    // State for file load timing
    QDateTime m_loadStartTime;
    double m_loadStartMemRss = 0.0;
};
