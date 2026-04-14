// Logger.cpp — ProctorOps
// Structured JSON-lines logger with automatic PII scrubbing.

#include "Logger.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>

#include <cstdio>

// Field names that must be auto-redacted in log context objects
static const QStringList s_sensitiveFields = {
    QStringLiteral("mobile"),
    QStringLiteral("barcode"),
    QStringLiteral("password"),
    QStringLiteral("secret"),
    QStringLiteral("key"),
    QStringLiteral("hash"),
    QStringLiteral("saltHex"),
    QStringLiteral("hashHex"),
    QStringLiteral("token"),
    QStringLiteral("answerHashHex"),
};

static const QStringList s_identifierFields = {
    QStringLiteral("member_id"),
    QStringLiteral("session_id"),
    QStringLiteral("user_id"),
    QStringLiteral("actor_user_id"),
    QStringLiteral("step_up_window_id"),
};

namespace {

bool containsField(const QStringList& fields, const QString& key)
{
    for (const QString& field : fields) {
        if (key.compare(field, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString hashedIdentifier(const QJsonValue& value)
{
    const QString text = value.toVariant().toString();
    const QByteArray digest = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("[HASH:%1]").arg(QString::fromLatin1(digest.left(12)));
}

QJsonValue scrubValue(const QString& key, const QJsonValue& value)
{
    if (containsField(s_sensitiveFields, key))
        return QStringLiteral("[REDACTED]");
    if (containsField(s_identifierFields, key))
        return hashedIdentifier(value);

    if (value.isObject()) {
        QJsonObject scrubbedObject;
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            scrubbedObject[it.key()] = scrubValue(it.key(), it.value());
        return scrubbedObject;
    }

    if (value.isArray()) {
        QJsonArray scrubbedArray;
        for (const QJsonValue& item : value.toArray())
            scrubbedArray.append(scrubValue(key, item));
        return scrubbedArray;
    }

    return value;
}

}

Logger& Logger::instance()
{
    static Logger s_instance;
    return s_instance;
}

Logger::Logger() = default;

Logger::~Logger()
{
    delete m_file;
}

void Logger::setOutputPath(const QString& path)
{
    delete m_file;
    m_file = new QFile(path);
    m_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void Logger::info(const QString& component, const QString& message,
                   const QJsonObject& context)
{
    log(Level::Info, component, message, context);
}

void Logger::info(const QString& message, const QJsonObject& context)
{
    log(Level::Info, QStringLiteral("General"), message, context);
}

void Logger::warn(const QString& component, const QString& message,
                   const QJsonObject& context)
{
    log(Level::Warn, component, message, context);
}

void Logger::warn(const QString& message, const QJsonObject& context)
{
    log(Level::Warn, QStringLiteral("General"), message, context);
}

void Logger::error(const QString& component, const QString& message,
                    const QJsonObject& context)
{
    log(Level::Error, component, message, context);
}

void Logger::error(const QString& message, const QJsonObject& context)
{
    log(Level::Error, QStringLiteral("General"), message, context);
}

void Logger::security(const QString& component, const QString& message,
                       const QJsonObject& context)
{
    log(Level::Security, component, message, context);
}

void Logger::security(const QString& message, const QJsonObject& context)
{
    log(Level::Security, QStringLiteral("General"), message, context);
}

void Logger::log(Level level, const QString& component, const QString& message,
                  const QJsonObject& context)
{
    QJsonObject entry;
    entry[QStringLiteral("ts")] = QDateTime::currentDateTimeUtc()
                                       .toString(Qt::ISODateWithMs);
    entry[QStringLiteral("level")] = levelToString(level);
    entry[QStringLiteral("component")] = component;
    entry[QStringLiteral("msg")] = message;

    if (!context.isEmpty())
        entry[QStringLiteral("ctx")] = scrubContext(context);

    QByteArray line = QJsonDocument(entry).toJson(QJsonDocument::Compact);
    line.append('\n');

    if (m_file && m_file->isOpen()) {
        m_file->write(line);
        m_file->flush();
    } else {
        fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
        fflush(stderr);
    }
}

QJsonObject Logger::scrubContext(const QJsonObject& context)
{
    QJsonObject scrubbed;
    for (auto it = context.begin(); it != context.end(); ++it)
        scrubbed[it.key()] = scrubValue(it.key(), it.value());
    return scrubbed;
}

QString Logger::redactPii(const QString& value)
{
    if (value.length() <= 4)
        return QString(value.length(), QChar('*'));

    return QString(value.length() - 4, QChar('*')) + value.right(4);
}

QString Logger::redactSecret(const QString&)
{
    return QStringLiteral("[REDACTED]");
}

QString Logger::levelToString(Level level)
{
    switch (level) {
    case Level::Info:     return QStringLiteral("INFO");
    case Level::Warn:     return QStringLiteral("WARN");
    case Level::Error:    return QStringLiteral("ERROR");
    case Level::Security: return QStringLiteral("SECURITY");
    }
    return QStringLiteral("UNKNOWN");
}
