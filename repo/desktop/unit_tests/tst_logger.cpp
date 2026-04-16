// tst_logger.cpp — ProctorOps
// Unit tests for structured logger output and context scrubbing.

#include <QtTest/QtTest>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>

#include "utils/Logger.h"

class TstLogger : public QObject {
    Q_OBJECT

private slots:
    void test_redactHelpers();
    void test_sensitiveFieldsAreRedacted();
    void test_identifierFieldsAreHashed();
    void test_nestedObjectsAndArraysScrubbed();

private:
    QJsonObject readLastEntry(const QString& filePath) const;
};

QJsonObject TstLogger::readLastEntry(const QString& filePath) const
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QList<QByteArray> lines = f.readAll().trimmed().split('\n');
    if (lines.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(lines.last());
    if (!doc.isObject())
        return {};
    return doc.object();
}

void TstLogger::test_redactHelpers()
{
    QCOMPARE(Logger::redactPii(QStringLiteral("1234567890")), QStringLiteral("******7890"));
    QCOMPARE(Logger::redactPii(QStringLiteral("1234")), QStringLiteral("****"));
    QCOMPARE(Logger::redactSecret(QStringLiteral("secret")), QStringLiteral("[REDACTED]"));
}

void TstLogger::test_sensitiveFieldsAreRedacted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("app.log"));

    Logger::instance().setOutputPath(logPath);
    Logger::instance().security(QStringLiteral("auth"),
                                QStringLiteral("login failed"),
                                {
                                    {QStringLiteral("password"), QStringLiteral("TopSecret")},
                                    {QStringLiteral("token"), QStringLiteral("abc123")},
                                    {QStringLiteral("mobile"), QStringLiteral("5551230000")}
                                });

    const QJsonObject entry = readLastEntry(logPath);
    QVERIFY(entry.contains(QStringLiteral("ctx")));
    const QJsonObject ctx = entry.value(QStringLiteral("ctx")).toObject();
    QCOMPARE(ctx.value(QStringLiteral("password")).toString(), QStringLiteral("[REDACTED]"));
    QCOMPARE(ctx.value(QStringLiteral("token")).toString(), QStringLiteral("[REDACTED]"));
    QCOMPARE(ctx.value(QStringLiteral("mobile")).toString(), QStringLiteral("[REDACTED]"));
}

void TstLogger::test_identifierFieldsAreHashed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("app.log"));

    Logger::instance().setOutputPath(logPath);
    Logger::instance().info(QStringLiteral("sync"),
                            QStringLiteral("event"),
                            {
                                {QStringLiteral("member_id"), QStringLiteral("M-100")},
                                {QStringLiteral("session_id"), QStringLiteral("S-200")}
                            });

    const QJsonObject entry = readLastEntry(logPath);
    const QJsonObject ctx = entry.value(QStringLiteral("ctx")).toObject();
    const QString memberHash = ctx.value(QStringLiteral("member_id")).toString();
    const QString sessionHash = ctx.value(QStringLiteral("session_id")).toString();
    QVERIFY(memberHash.startsWith(QStringLiteral("[HASH:")));
    QVERIFY(memberHash.endsWith(QStringLiteral("]")));
    QVERIFY(sessionHash.startsWith(QStringLiteral("[HASH:")));
    QVERIFY(sessionHash.endsWith(QStringLiteral("]")));
}

void TstLogger::test_nestedObjectsAndArraysScrubbed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("app.log"));

    QJsonObject nested;
    nested.insert(QStringLiteral("secret"), QStringLiteral("dontlog"));

    QJsonArray values;
    values.append(QStringLiteral("x"));
    values.append(QStringLiteral("y"));

    Logger::instance().setOutputPath(logPath);
    Logger::instance().warn(QStringLiteral("audit"),
                            QStringLiteral("nested"),
                            {
                                {QStringLiteral("details"), nested},
                                {QStringLiteral("hash"), QStringLiteral("abcdef")},
                                {QStringLiteral("values"), values}
                            });

    const QJsonObject entry = readLastEntry(logPath);
    const QJsonObject ctx = entry.value(QStringLiteral("ctx")).toObject();
    QCOMPARE(ctx.value(QStringLiteral("hash")).toString(), QStringLiteral("[REDACTED]"));
    const QJsonObject details = ctx.value(QStringLiteral("details")).toObject();
    QCOMPARE(details.value(QStringLiteral("secret")).toString(), QStringLiteral("[REDACTED]"));
    QVERIFY(ctx.value(QStringLiteral("values")).isArray());
}

QTEST_APPLESS_MAIN(TstLogger)
#include "tst_logger.moc"
