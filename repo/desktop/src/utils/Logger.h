#pragma once
// Logger.h — ProctorOps
// Structured JSON-lines application logger with secret-safe PII scrubbing.
// Output format: {"ts":"ISO8601","level":"...","component":"...","msg":"...","ctx":{...}}
// Fields named mobile, barcode, password, secret, key, hash are auto-redacted in context.

#include <QString>
#include <QJsonObject>

class QFile;

class Logger {
public:
    /// Access the singleton logger instance.
    static Logger& instance();

    enum class Level { Info, Warn, Error, Security };

    void info(const QString& component, const QString& message,
              const QJsonObject& context = {});
    void info(const QString& message, const QJsonObject& context = {});
    void warn(const QString& component, const QString& message,
              const QJsonObject& context = {});
    void warn(const QString& message, const QJsonObject& context = {});
    void error(const QString& component, const QString& message,
               const QJsonObject& context = {});
    void error(const QString& message, const QJsonObject& context = {});
    void security(const QString& component, const QString& message,
                  const QJsonObject& context = {});
    void security(const QString& message, const QJsonObject& context = {});

    /// Set the output file path. If not set, logs to stderr.
    void setOutputPath(const QString& path);

    /// Replace all but the last 4 characters with '*'. Returns the masked string.
    [[nodiscard]] static QString redactPii(const QString& value);

    /// Replace entire value with [REDACTED].
    [[nodiscard]] static QString redactSecret(const QString& value);

    // Non-copyable, non-movable
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();

    void log(Level level, const QString& component, const QString& message,
             const QJsonObject& context);

    /// Scrub sensitive fields from a context object before serialization.
    [[nodiscard]] static QJsonObject scrubContext(const QJsonObject& context);

    static QString levelToString(Level level);

    QFile* m_file = nullptr;
};
