// tst_error_formatter.cpp — ProctorOps
// Unit tests for ErrorFormatter — every ErrorCode branch in toUserMessage,
// toFormHint, isSecurityError, and requiresStepUp.
//
// ErrorFormatter is pure static logic with no I/O dependencies, so every
// code path can be exercised directly without mocks.

#include <QtTest/QtTest>
#include "utils/ErrorFormatter.h"

class TstErrorFormatter : public QObject
{
    Q_OBJECT

private slots:
    // ── toUserMessage ────────────────────────────────────────────────────
    void test_toUserMessage_coversEveryErrorCode_data();
    void test_toUserMessage_coversEveryErrorCode();
    void test_toUserMessage_appendsDetailWhenProvided();
    void test_toUserMessage_omitsDetailWhenEmpty();

    // ── toFormHint ───────────────────────────────────────────────────────
    void test_toFormHint_mappedCodes_data();
    void test_toFormHint_mappedCodes();
    void test_toFormHint_defaultForUnmappedCode();

    // ── isSecurityError ──────────────────────────────────────────────────
    void test_isSecurityError_securityCodes_data();
    void test_isSecurityError_securityCodes();
    void test_isSecurityError_nonSecurityCodes_data();
    void test_isSecurityError_nonSecurityCodes();

    // ── requiresStepUp ───────────────────────────────────────────────────
    void test_requiresStepUp_stepUpCode();
    void test_requiresStepUp_otherCodes_data();
    void test_requiresStepUp_otherCodes();
};

// ── toUserMessage ────────────────────────────────────────────────────────────

void TstErrorFormatter::test_toUserMessage_coversEveryErrorCode_data()
{
    QTest::addColumn<int>("code");
    QTest::addColumn<QString>("substring");

    QTest::newRow("NotFound")            << int(ErrorCode::NotFound)            << QStringLiteral("not found");
    QTest::newRow("AlreadyExists")       << int(ErrorCode::AlreadyExists)       << QStringLiteral("already exists");
    QTest::newRow("ValidationFailed")    << int(ErrorCode::ValidationFailed)    << QStringLiteral("Validation failed");
    QTest::newRow("InternalError")       << int(ErrorCode::InternalError)       << QStringLiteral("internal error");
    QTest::newRow("DbError")             << int(ErrorCode::DbError)             << QStringLiteral("database error");
    QTest::newRow("AuthorizationDenied") << int(ErrorCode::AuthorizationDenied) << QStringLiteral("permission");
    QTest::newRow("StepUpRequired")      << int(ErrorCode::StepUpRequired)      << QStringLiteral("re-authentication");
    QTest::newRow("AccountLocked")       << int(ErrorCode::AccountLocked)       << QStringLiteral("locked");
    QTest::newRow("CaptchaRequired")     << int(ErrorCode::CaptchaRequired)     << QStringLiteral("security challenge");
    QTest::newRow("InvalidCredentials")  << int(ErrorCode::InvalidCredentials)  << QStringLiteral("Invalid username");
    QTest::newRow("DuplicateCheckIn")    << int(ErrorCode::DuplicateCheckIn)    << QStringLiteral("already checked in");
    QTest::newRow("TermCardExpired")     << int(ErrorCode::TermCardExpired)     << QStringLiteral("term card has expired");
    QTest::newRow("TermCardMissing")     << int(ErrorCode::TermCardMissing)     << QStringLiteral("No active term card");
    QTest::newRow("AccountFrozen")       << int(ErrorCode::AccountFrozen)       << QStringLiteral("frozen");
    QTest::newRow("PunchCardExhausted")  << int(ErrorCode::PunchCardExhausted)  << QStringLiteral("No remaining sessions");
    QTest::newRow("SignatureInvalid")    << int(ErrorCode::SignatureInvalid)    << QStringLiteral("signature is invalid");
    QTest::newRow("TrustStoreMiss")      << int(ErrorCode::TrustStoreMiss)      << QStringLiteral("trust store");
    QTest::newRow("PackageCorrupt")      << int(ErrorCode::PackageCorrupt)      << QStringLiteral("corrupt");
    QTest::newRow("EncryptionFailed")    << int(ErrorCode::EncryptionFailed)    << QStringLiteral("encryption failed");
    QTest::newRow("DecryptionFailed")    << int(ErrorCode::DecryptionFailed)    << QStringLiteral("decryption failed");
    QTest::newRow("KeyNotFound")         << int(ErrorCode::KeyNotFound)         << QStringLiteral("encryption key was not found");
    QTest::newRow("ConflictUnresolved")  << int(ErrorCode::ConflictUnresolved)  << QStringLiteral("unresolved conflicts");
    QTest::newRow("JobDependencyUnmet")  << int(ErrorCode::JobDependencyUnmet)  << QStringLiteral("prerequisite job");
    QTest::newRow("CheckpointCorrupt")   << int(ErrorCode::CheckpointCorrupt)   << QStringLiteral("checkpoint data is corrupt");
    QTest::newRow("IoError")             << int(ErrorCode::IoError)             << QStringLiteral("input/output error");
    QTest::newRow("InvalidState")        << int(ErrorCode::InvalidState)        << QStringLiteral("not allowed in the current state");
    QTest::newRow("ChainIntegrityFailed")<< int(ErrorCode::ChainIntegrityFailed)<< QStringLiteral("Audit chain");
}

void TstErrorFormatter::test_toUserMessage_coversEveryErrorCode()
{
    QFETCH(int, code);
    QFETCH(QString, substring);

    const QString msg = ErrorFormatter::toUserMessage(static_cast<ErrorCode>(code));
    QVERIFY2(!msg.isEmpty(), qPrintable(QStringLiteral("empty message for code %1").arg(code)));
    QVERIFY2(msg.contains(substring, Qt::CaseInsensitive),
             qPrintable(QStringLiteral("message '%1' missing substring '%2'").arg(msg, substring)));
}

void TstErrorFormatter::test_toUserMessage_appendsDetailWhenProvided()
{
    const QString detail = QStringLiteral("user-002");
    const QString msg = ErrorFormatter::toUserMessage(ErrorCode::NotFound, detail);
    QVERIFY(msg.contains(detail));
    QVERIFY(msg.endsWith(detail));
}

void TstErrorFormatter::test_toUserMessage_omitsDetailWhenEmpty()
{
    const QString msg = ErrorFormatter::toUserMessage(ErrorCode::NotFound, QString());
    QCOMPARE(msg, ErrorFormatter::toUserMessage(ErrorCode::NotFound));
    QVERIFY(!msg.endsWith(QChar(' ')));
}

// ── toFormHint ───────────────────────────────────────────────────────────────

void TstErrorFormatter::test_toFormHint_mappedCodes_data()
{
    QTest::addColumn<int>("code");
    QTest::addColumn<QString>("expected");

    QTest::newRow("ValidationFailed")    << int(ErrorCode::ValidationFailed)    << QStringLiteral("Invalid input");
    QTest::newRow("InvalidCredentials")  << int(ErrorCode::InvalidCredentials)  << QStringLiteral("Wrong username or password");
    QTest::newRow("AccountLocked")       << int(ErrorCode::AccountLocked)       << QStringLiteral("Account locked");
    QTest::newRow("CaptchaRequired")     << int(ErrorCode::CaptchaRequired)     << QStringLiteral("Security challenge required");
    QTest::newRow("StepUpRequired")      << int(ErrorCode::StepUpRequired)      << QStringLiteral("Re-authentication required");
    QTest::newRow("AuthorizationDenied") << int(ErrorCode::AuthorizationDenied) << QStringLiteral("Insufficient permissions");
    QTest::newRow("AlreadyExists")       << int(ErrorCode::AlreadyExists)       << QStringLiteral("Already exists");
    QTest::newRow("NotFound")            << int(ErrorCode::NotFound)            << QStringLiteral("Not found");
}

void TstErrorFormatter::test_toFormHint_mappedCodes()
{
    QFETCH(int, code);
    QFETCH(QString, expected);
    QCOMPARE(ErrorFormatter::toFormHint(static_cast<ErrorCode>(code)), expected);
}

void TstErrorFormatter::test_toFormHint_defaultForUnmappedCode()
{
    // Unmapped codes fall through to the default branch.
    QCOMPARE(ErrorFormatter::toFormHint(ErrorCode::InternalError), QStringLiteral("Error"));
    QCOMPARE(ErrorFormatter::toFormHint(ErrorCode::DbError),       QStringLiteral("Error"));
    QCOMPARE(ErrorFormatter::toFormHint(ErrorCode::SignatureInvalid), QStringLiteral("Error"));
    QCOMPARE(ErrorFormatter::toFormHint(ErrorCode::IoError),       QStringLiteral("Error"));
}

// ── isSecurityError ──────────────────────────────────────────────────────────

void TstErrorFormatter::test_isSecurityError_securityCodes_data()
{
    QTest::addColumn<int>("code");

    QTest::newRow("AuthorizationDenied")  << int(ErrorCode::AuthorizationDenied);
    QTest::newRow("StepUpRequired")       << int(ErrorCode::StepUpRequired);
    QTest::newRow("AccountLocked")        << int(ErrorCode::AccountLocked);
    QTest::newRow("CaptchaRequired")      << int(ErrorCode::CaptchaRequired);
    QTest::newRow("InvalidCredentials")   << int(ErrorCode::InvalidCredentials);
    QTest::newRow("SignatureInvalid")     << int(ErrorCode::SignatureInvalid);
    QTest::newRow("TrustStoreMiss")       << int(ErrorCode::TrustStoreMiss);
    QTest::newRow("EncryptionFailed")     << int(ErrorCode::EncryptionFailed);
    QTest::newRow("DecryptionFailed")     << int(ErrorCode::DecryptionFailed);
    QTest::newRow("KeyNotFound")          << int(ErrorCode::KeyNotFound);
    QTest::newRow("ChainIntegrityFailed") << int(ErrorCode::ChainIntegrityFailed);
}

void TstErrorFormatter::test_isSecurityError_securityCodes()
{
    QFETCH(int, code);
    QVERIFY(ErrorFormatter::isSecurityError(static_cast<ErrorCode>(code)));
}

void TstErrorFormatter::test_isSecurityError_nonSecurityCodes_data()
{
    QTest::addColumn<int>("code");

    QTest::newRow("NotFound")           << int(ErrorCode::NotFound);
    QTest::newRow("AlreadyExists")      << int(ErrorCode::AlreadyExists);
    QTest::newRow("ValidationFailed")   << int(ErrorCode::ValidationFailed);
    QTest::newRow("InternalError")      << int(ErrorCode::InternalError);
    QTest::newRow("DbError")            << int(ErrorCode::DbError);
    QTest::newRow("DuplicateCheckIn")   << int(ErrorCode::DuplicateCheckIn);
    QTest::newRow("TermCardExpired")    << int(ErrorCode::TermCardExpired);
    QTest::newRow("TermCardMissing")    << int(ErrorCode::TermCardMissing);
    QTest::newRow("AccountFrozen")      << int(ErrorCode::AccountFrozen);
    QTest::newRow("PunchCardExhausted") << int(ErrorCode::PunchCardExhausted);
    QTest::newRow("PackageCorrupt")     << int(ErrorCode::PackageCorrupt);
    QTest::newRow("ConflictUnresolved") << int(ErrorCode::ConflictUnresolved);
    QTest::newRow("JobDependencyUnmet") << int(ErrorCode::JobDependencyUnmet);
    QTest::newRow("CheckpointCorrupt")  << int(ErrorCode::CheckpointCorrupt);
    QTest::newRow("IoError")            << int(ErrorCode::IoError);
    QTest::newRow("InvalidState")       << int(ErrorCode::InvalidState);
}

void TstErrorFormatter::test_isSecurityError_nonSecurityCodes()
{
    QFETCH(int, code);
    QVERIFY(!ErrorFormatter::isSecurityError(static_cast<ErrorCode>(code)));
}

// ── requiresStepUp ───────────────────────────────────────────────────────────

void TstErrorFormatter::test_requiresStepUp_stepUpCode()
{
    QVERIFY(ErrorFormatter::requiresStepUp(ErrorCode::StepUpRequired));
}

void TstErrorFormatter::test_requiresStepUp_otherCodes_data()
{
    QTest::addColumn<int>("code");

    // Every other code should not trigger a step-up requirement.
    QTest::newRow("NotFound")             << int(ErrorCode::NotFound);
    QTest::newRow("AuthorizationDenied")  << int(ErrorCode::AuthorizationDenied);
    QTest::newRow("AccountLocked")        << int(ErrorCode::AccountLocked);
    QTest::newRow("CaptchaRequired")      << int(ErrorCode::CaptchaRequired);
    QTest::newRow("InvalidCredentials")   << int(ErrorCode::InvalidCredentials);
    QTest::newRow("ValidationFailed")     << int(ErrorCode::ValidationFailed);
    QTest::newRow("InternalError")        << int(ErrorCode::InternalError);
    QTest::newRow("DbError")              << int(ErrorCode::DbError);
}

void TstErrorFormatter::test_requiresStepUp_otherCodes()
{
    QFETCH(int, code);
    QVERIFY(!ErrorFormatter::requiresStepUp(static_cast<ErrorCode>(code)));
}

QTEST_APPLESS_MAIN(TstErrorFormatter)
#include "tst_error_formatter.moc"
