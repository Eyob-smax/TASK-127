// tst_captcha_generator.cpp — ProctorOps
// Unit tests for local CAPTCHA generation and answer verification.

#include <QtTest/QtTest>

#include <QRegularExpression>

#include "utils/CaptchaGenerator.h"

class TstCaptchaGenerator : public QObject {
    Q_OBJECT

private slots:
    void test_generate_populatesChallengeFields();
    void test_generate_answerShape();
    void test_verifyAnswer_acceptsTrimmedCaseInsensitive();
    void test_verifyAnswer_rejectsWrongAnswer();
};

void TstCaptchaGenerator::test_generate_populatesChallengeFields()
{
    const CaptchaChallenge challenge = CaptchaGenerator::generate();
    QVERIFY(!challenge.challengeId.isEmpty());
    QVERIFY(!challenge.answerText.isEmpty());
    QVERIFY(!challenge.answerHashHex.isEmpty());
    QCOMPARE(challenge.image.width(), 200);
    QCOMPARE(challenge.image.height(), 80);
}

void TstCaptchaGenerator::test_generate_answerShape()
{
    const CaptchaChallenge challenge = CaptchaGenerator::generate();
    QVERIFY(challenge.answerText.size() == 5 || challenge.answerText.size() == 6);

    const QRegularExpression re(QStringLiteral("^[A-Z2-9]+$"));
    QVERIFY(re.match(challenge.answerText).hasMatch());
    QCOMPARE(challenge.answerHashHex.size(), 64);
}

void TstCaptchaGenerator::test_verifyAnswer_acceptsTrimmedCaseInsensitive()
{
    const CaptchaChallenge challenge = CaptchaGenerator::generate();
    const QString variant = QStringLiteral("  %1  ").arg(challenge.answerText.toLower());
    QVERIFY(CaptchaGenerator::verifyAnswer(variant, challenge.answerHashHex));
}

void TstCaptchaGenerator::test_verifyAnswer_rejectsWrongAnswer()
{
    const CaptchaChallenge challenge = CaptchaGenerator::generate();
    QVERIFY(!CaptchaGenerator::verifyAnswer(QStringLiteral("WRONG"), challenge.answerHashHex));
}

QTEST_MAIN(TstCaptchaGenerator)
#include "tst_captcha_generator.moc"
