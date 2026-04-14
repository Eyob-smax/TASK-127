// tst_domain_validation.cpp — ProctorOps
// Unit tests for canonical business invariants defined in Validation.h.
// Tests cover: password policy, mobile number format, difficulty/discrimination
// ranges, duplicate window, lockout timing, CAPTCHA timing, step-up timing,
// scheduler retry backoff, and all constexpr boundary values.

#include <QtTest/QtTest>
#include "utils/Validation.h"
#include "models/User.h"
#include "models/Member.h"
#include "models/Question.h"
#include "models/Ingestion.h"

class TstDomainValidation : public QObject
{
    Q_OBJECT

private slots:
    // ── Password policy ───────────────────────────────────────────────────
    void test_passwordMinLength_boundary();
    void test_passwordMinLength_exact();
    void test_passwordMinLength_belowMin();

    // ── Lockout policy ────────────────────────────────────────────────────
    void test_lockoutConstants();

    // ── CAPTCHA policy ────────────────────────────────────────────────────
    void test_captchaConstants();

    // ── Step-up window ────────────────────────────────────────────────────
    void test_stepUpWindowSeconds();

    // ── Mobile number format ──────────────────────────────────────────────
    void test_mobileValid_canonical();
    void test_mobileInvalid_missingParens();
    void test_mobileInvalid_wrongSeparator();
    void test_mobileInvalid_letters();
    void test_mobileInvalid_tooShort();

    // ── Duplicate window ──────────────────────────────────────────────────
    void test_duplicateWindowSeconds();

    // ── Question difficulty ───────────────────────────────────────────────
    void test_difficultyMin();
    void test_difficultyMax();
    void test_difficultyBelowMin();
    void test_difficultyAboveMax();
    void test_difficultyBoundary_1();
    void test_difficultyBoundary_5();

    // ── Question discrimination ───────────────────────────────────────────
    void test_discriminationMin();
    void test_discriminationMax();
    void test_discriminationBelowMin();
    void test_discriminationAboveMax();
    void test_discriminationBoundary_0();
    void test_discriminationBoundary_1();

    // ── Scheduler retry backoff ───────────────────────────────────────────
    void test_retryDelay_first();
    void test_retryDelay_second();
    void test_retryDelay_thirdAndBeyond();
    void test_schedulerDefaultWorkers();
    void test_schedulerMaxRetries();

    // ── Argon2id defaults ─────────────────────────────────────────────────
    void test_argon2idDefaults();
};

// ── Password policy ────────────────────────────────────────────────────────

void TstDomainValidation::test_passwordMinLength_boundary()
{
    QCOMPARE(Validation::PasswordMinLength, 12);
}

void TstDomainValidation::test_passwordMinLength_exact()
{
    const QString pw12(12, QChar('a'));
    QVERIFY(Validation::isPasswordLengthValid(pw12));
}

void TstDomainValidation::test_passwordMinLength_belowMin()
{
    const QString pw11(11, QChar('a'));
    QVERIFY(!Validation::isPasswordLengthValid(pw11));
    const QString empty;
    QVERIFY(!Validation::isPasswordLengthValid(empty));
}

// ── Lockout policy ──────────────────────────────────────────────────────────

void TstDomainValidation::test_lockoutConstants()
{
    QCOMPARE(Validation::LockoutFailureThreshold, 5);
    QCOMPARE(Validation::LockoutWindowSeconds, 600);    // 10 minutes
    QCOMPARE(Validation::CaptchaAfterFailures, 3);
    QCOMPARE(Validation::CaptchaCooldownSeconds, 900);  // 15 minutes
    // CAPTCHA threshold must be strictly less than lockout threshold
    QVERIFY(Validation::CaptchaAfterFailures < Validation::LockoutFailureThreshold);
}

// ── CAPTCHA policy ──────────────────────────────────────────────────────────

void TstDomainValidation::test_captchaConstants()
{
    QCOMPARE(Validation::CaptchaCooldownSeconds, 900);
}

// ── Step-up window ──────────────────────────────────────────────────────────

void TstDomainValidation::test_stepUpWindowSeconds()
{
    QCOMPARE(Validation::StepUpWindowSeconds, 120);  // 2 minutes
}

// ── Mobile number format ────────────────────────────────────────────────────

void TstDomainValidation::test_mobileValid_canonical()
{
    QVERIFY(Validation::isMobileValid(QStringLiteral("(021) 555-1234")));
    QVERIFY(Validation::isMobileValid(QStringLiteral("(800) 123-4567")));
    QVERIFY(Validation::isMobileValid(QStringLiteral("(999) 000-0000")));
}

void TstDomainValidation::test_mobileInvalid_missingParens()
{
    QVERIFY(!Validation::isMobileValid(QStringLiteral("021 555-1234")));
    QVERIFY(!Validation::isMobileValid(QStringLiteral("021-555-1234")));
}

void TstDomainValidation::test_mobileInvalid_wrongSeparator()
{
    QVERIFY(!Validation::isMobileValid(QStringLiteral("(021)555-1234")));   // missing space
    QVERIFY(!Validation::isMobileValid(QStringLiteral("(021) 555 1234")));  // wrong separator
}

void TstDomainValidation::test_mobileInvalid_letters()
{
    QVERIFY(!Validation::isMobileValid(QStringLiteral("(0A1) 555-1234")));
}

void TstDomainValidation::test_mobileInvalid_tooShort()
{
    QVERIFY(!Validation::isMobileValid(QStringLiteral("(021) 55-1234")));
    QVERIFY(!Validation::isMobileValid(QString{}));
}

// ── Duplicate window ────────────────────────────────────────────────────────

void TstDomainValidation::test_duplicateWindowSeconds()
{
    QCOMPARE(Validation::DuplicateWindowSeconds, 30);
}

// ── Question difficulty ─────────────────────────────────────────────────────

void TstDomainValidation::test_difficultyMin()
{
    QCOMPARE(Validation::DifficultyMin, 1);
}

void TstDomainValidation::test_difficultyMax()
{
    QCOMPARE(Validation::DifficultyMax, 5);
}

void TstDomainValidation::test_difficultyBelowMin()
{
    QVERIFY(!Validation::isDifficultyValid(0));
    QVERIFY(!Validation::isDifficultyValid(-1));
}

void TstDomainValidation::test_difficultyAboveMax()
{
    QVERIFY(!Validation::isDifficultyValid(6));
    QVERIFY(!Validation::isDifficultyValid(100));
}

void TstDomainValidation::test_difficultyBoundary_1()
{
    QVERIFY(Validation::isDifficultyValid(1));
}

void TstDomainValidation::test_difficultyBoundary_5()
{
    QVERIFY(Validation::isDifficultyValid(5));
}

// ── Question discrimination ─────────────────────────────────────────────────

void TstDomainValidation::test_discriminationMin()
{
    QCOMPARE(Validation::DiscriminationMin, 0.00);
}

void TstDomainValidation::test_discriminationMax()
{
    QCOMPARE(Validation::DiscriminationMax, 1.00);
}

void TstDomainValidation::test_discriminationBelowMin()
{
    QVERIFY(!Validation::isDiscriminationValid(-0.01));
    QVERIFY(!Validation::isDiscriminationValid(-1.0));
}

void TstDomainValidation::test_discriminationAboveMax()
{
    QVERIFY(!Validation::isDiscriminationValid(1.01));
    QVERIFY(!Validation::isDiscriminationValid(2.0));
}

void TstDomainValidation::test_discriminationBoundary_0()
{
    QVERIFY(Validation::isDiscriminationValid(0.00));
}

void TstDomainValidation::test_discriminationBoundary_1()
{
    QVERIFY(Validation::isDiscriminationValid(1.00));
}

// ── Scheduler retry backoff ─────────────────────────────────────────────────

void TstDomainValidation::test_retryDelay_first()
{
    QCOMPARE(Validation::retryDelaySeconds(0), Validation::RetryDelay1Seconds);
    QCOMPARE(Validation::RetryDelay1Seconds, 5);
}

void TstDomainValidation::test_retryDelay_second()
{
    QCOMPARE(Validation::retryDelaySeconds(1), Validation::RetryDelay2Seconds);
    QCOMPARE(Validation::RetryDelay2Seconds, 30);
}

void TstDomainValidation::test_retryDelay_thirdAndBeyond()
{
    QCOMPARE(Validation::retryDelaySeconds(2), Validation::RetryDelay3Seconds);
    QCOMPARE(Validation::retryDelaySeconds(3), Validation::RetryDelay3Seconds);
    QCOMPARE(Validation::retryDelaySeconds(10), Validation::RetryDelay3Seconds);
    QCOMPARE(Validation::RetryDelay3Seconds, 120);  // 2 minutes
}

void TstDomainValidation::test_schedulerDefaultWorkers()
{
    QCOMPARE(Validation::SchedulerDefaultWorkers, 2);
    QVERIFY(Validation::SchedulerDefaultWorkers >= Validation::SchedulerMinWorkers);
    QVERIFY(Validation::SchedulerDefaultWorkers <= Validation::SchedulerMaxWorkers);
}

void TstDomainValidation::test_schedulerMaxRetries()
{
    QCOMPARE(Validation::SchedulerMaxRetries, 5);
}

// ── Argon2id defaults ───────────────────────────────────────────────────────

void TstDomainValidation::test_argon2idDefaults()
{
    QCOMPARE(Validation::Argon2TimeCost,    3);
    QCOMPARE(Validation::Argon2MemoryCost,  65536);   // 64 MB
    QCOMPARE(Validation::Argon2Parallelism, 4);
    QCOMPARE(Validation::Argon2TagLength,   32);      // 256 bits
    QCOMPARE(Validation::Argon2SaltLength,  16);      // 128 bits
    // Memory cost must be at least 64 MB for security
    QVERIFY(Validation::Argon2MemoryCost >= 65536);
}

QTEST_GUILESS_MAIN(TstDomainValidation)
#include "tst_domain_validation.moc"
