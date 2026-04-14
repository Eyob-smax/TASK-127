// PerformanceObserver.cpp — ProctorOps

#include "utils/PerformanceObserver.h"
#include "utils/Logger.h"
#include <QSqlQuery>
#include <QDateTime>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <psapi.h>
#endif

#ifdef Q_OS_LINUX
#  include <QFile>
#endif

PerformanceObserver::PerformanceObserver(QSqlDatabase& db, QObject* parent)
    : QObject(parent), m_db(db)
{
    m_sampleTimer.setSingleShot(false);
    connect(&m_sampleTimer, &QTimer::timeout,
            this, &PerformanceObserver::onSampleTimer);
}

void PerformanceObserver::recordColdStart(qint64 elapsedMs)
{
    recordEvent(QStringLiteral("cold_start"), elapsedMs, 0);

    const bool withinTarget = elapsedMs < 3000;
    Logger::instance().info("Cold-start timing recorded",
        {{"elapsed_ms",   QString::number(elapsedMs)},
         {"target_ms",    QStringLiteral("3000")},
         {"verdict",      withinTarget
                          ? QStringLiteral("within_target")
                          : QStringLiteral("exceeds_target")},
         {"note", QStringLiteral("manual verification required on representative office PC")}});
}

void PerformanceObserver::startMemoryObservation(int sampleIntervalMs)
{
    m_sampleTimer.start(sampleIntervalMs);
}

void PerformanceObserver::stopMemoryObservation()
{
    m_sampleTimer.stop();
    recordEvent(QStringLiteral("shutdown"), 0, currentRssBytes());
}

qint64 PerformanceObserver::currentRssBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<qint64>(pmc.WorkingSetSize);
#endif

#ifdef Q_OS_LINUX
    QFile f(QStringLiteral("/proc/self/status"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QByteArray line = f.readLine();
            if (line.startsWith("VmRSS:")) {
                const auto parts = QString::fromLatin1(line).split(
                    QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.size() >= 2)
                    return parts[1].toLongLong() * 1024LL;
            }
        }
    }
#endif

    return 0LL;
}

void PerformanceObserver::onSampleTimer()
{
    const qint64 rss = currentRssBytes();
    recordEvent(QStringLiteral("memory_sample"), 0, rss);

    Logger::instance().info("Memory sample",
        {{"rss_bytes", QString::number(rss)},
         {"rss_mb",    QString::number(rss / (1024LL * 1024LL))},
         {"target_mb", QStringLiteral("200 MB growth limit over 7 days")},
         {"note",      QStringLiteral("manual verification required")}});
}

void PerformanceObserver::recordEvent(const QString& eventType,
                                       qint64 valueMs, qint64 valueBytes)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO performance_log (event_type, value_ms, value_bytes, recorded_at) "
        "VALUES (?, ?, ?, ?)"
    ));
    q.addBindValue(eventType);
    q.addBindValue(valueMs  > 0 ? QVariant(valueMs)    : QVariant());
    q.addBindValue(valueBytes > 0 ? QVariant(valueBytes) : QVariant());
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.exec();
}
