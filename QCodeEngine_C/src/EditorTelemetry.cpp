#include "CodeEditor/EditorTelemetry.h"
#include "CodeEditor/CodeEditor.h"
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QMutexLocker>
#include <unistd.h>
#include <fstream>

EditorTelemetry* EditorTelemetry::instance() {
    static EditorTelemetry* inst = new EditorTelemetry(qApp);
    return inst;
}

EditorTelemetry::EditorTelemetry(QObject* parent) : QObject(parent) {
}

EditorTelemetry::~EditorTelemetry() {
}

void EditorTelemetry::registerEditor(CodeEditor* editor) {
    QMutexLocker locker(&m_mutex);
    m_activeEditor = editor;
    locker.unlock();
    emit statsUpdated();
}

void EditorTelemetry::unregisterEditor(CodeEditor* editor) {
    QMutexLocker locker(&m_mutex);
    if (m_activeEditor == editor) {
        m_activeEditor = nullptr;
    }
    locker.unlock();
    emit statsUpdated();
}

CodeEditor* EditorTelemetry::activeEditor() const {
    QMutexLocker locker(&m_mutex);
    return m_activeEditor;
}

void EditorTelemetry::getMemoryUsage(double& vmUsageMb, double& rssUsageMb) const {
    vmUsageMb = 0.0;
    rssUsageMb = 0.0;
    
    std::ifstream statm("/proc/self/statm");
    if (!statm.is_open()) {
        return;
    }
    
    unsigned long long size = 0;
    unsigned long long resident = 0;
    statm >> size >> resident;
    statm.close();
    
    long pageSizeKb = sysconf(_SC_PAGE_SIZE) / 1024;
    vmUsageMb = (size * pageSizeKb) / 1024.0;
    rssUsageMb = (resident * pageSizeKb) / 1024.0;
}

void EditorTelemetry::recordOperation(const QString& opType, const QString& details, double durationMs) {
    double vmUsage = 0.0;
    double rssUsage = 0.0;
    getMemoryUsage(vmUsage, rssUsage);

    TelemetryLogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.opType = opType;
    entry.details = details;
    entry.durationMs = durationMs;
    entry.memAfterMb = rssUsage;
    entry.memBeforeMb = rssUsage;
    entry.memDiffMb = 0.0;

    {
        QMutexLocker locker(&m_mutex);
        m_logEntries.append(entry);
        if (m_logEntries.size() > 2000) {
            m_logEntries.removeFirst();
        }
    }

    emit entryLogged(entry);
    emit statsUpdated();
}

void EditorTelemetry::recordPaint(qint64 durationNs, double durationMs) {
    Q_UNUSED(durationNs);
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    
    {
        QMutexLocker locker(&m_mutex);
        QQueue<qint64>& times = const_cast<QQueue<qint64>&>(m_paintTimestamps);
        QQueue<double>& durs = const_cast<QQueue<double>&>(m_paintDurations);
        times.enqueue(now);
        durs.enqueue(durationMs);

        // Keep last 1 second of timestamps for FPS calculation
        while (!times.isEmpty() && now - times.head() > 1000) {
            times.dequeue();
        }
        
        // Keep last 50 durations
        while (durs.size() > 50) {
            durs.dequeue();
        }
    }

    // Log slow paints (takes > 12ms)
    if (durationMs > 12.0) {
        recordOperation("Paint (Slow)", QString("Duration: %1 ms").arg(durationMs, 0, 'f', 2), durationMs);
    } else {
        emit statsUpdated();
    }
}

void EditorTelemetry::startLoadFile(const QString& filePath) {
    Q_UNUSED(filePath);
    double vm = 0.0;
    getMemoryUsage(vm, m_loadStartMemRss);
    m_loadStartTime = QDateTime::currentDateTime();
}

void EditorTelemetry::endLoadFile(const QString& filePath, qint64 fileSize, int lines, int chars) {
    QDateTime now = QDateTime::currentDateTime();
    double duration = m_loadStartTime.msecsTo(now);
    
    double vm = 0.0;
    double rssEnd = 0.0;
    getMemoryUsage(vm, rssEnd);

    TelemetryLogEntry entry;
    entry.timestamp = now;
    entry.opType = "File Load";
    entry.details = QString("Path: %1, Size: %2 KB, Lines: %3, Chars: %4")
                        .arg(filePath)
                        .arg(fileSize / 1024.0, 0, 'f', 1)
                        .arg(lines)
                        .arg(chars);
    entry.durationMs = duration;
    entry.memBeforeMb = m_loadStartMemRss;
    entry.memAfterMb = rssEnd;
    entry.memDiffMb = rssEnd - m_loadStartMemRss;

    {
        QMutexLocker locker(&m_mutex);
        m_logEntries.append(entry);
        if (m_logEntries.size() > 2000) {
            m_logEntries.removeFirst();
        }
    }

    emit entryLogged(entry);
    emit statsUpdated();
}

double EditorTelemetry::currentFPS() const {
    QMutexLocker locker(&m_mutex);
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QQueue<qint64>& times = const_cast<QQueue<qint64>&>(m_paintTimestamps);
    while (!times.isEmpty() && now - times.head() > 1000) {
        times.dequeue();
    }
    return times.size();
}

double EditorTelemetry::averagePaintTimeMs() const {
    QMutexLocker locker(&m_mutex);
    if (m_paintDurations.isEmpty()) return 0.0;
    double sum = 0.0;
    for (double d : m_paintDurations) sum += d;
    return sum / m_paintDurations.size();
}

QList<TelemetryLogEntry> EditorTelemetry::logEntries() const {
    QMutexLocker locker(&m_mutex);
    return m_logEntries;
}

void EditorTelemetry::clearLog() {
    {
        QMutexLocker locker(&m_mutex);
        m_logEntries.clear();
    }
    emit logCleared();
    emit statsUpdated();
}
