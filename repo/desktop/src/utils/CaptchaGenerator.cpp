// CaptchaGenerator.cpp — ProctorOps
// CAPTCHA image generation with random alphanumeric text and visual distortion.

#include "CaptchaGenerator.h"
#include "crypto/SecureRandom.h"
#include "crypto/HashChain.h"

#include <QPainter>
#include <QFont>
#include <QUuid>
#include <QtMath>

namespace {

static const char s_charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
// Intentionally excludes I, O, 0, 1 to reduce ambiguity

QString generateRandomText(int length)
{
    QByteArray randomBytes = SecureRandom::generate(length);
    QString text;
    text.reserve(length);
    for (int i = 0; i < length; ++i) {
        int idx = static_cast<unsigned char>(randomBytes[i]) % (sizeof(s_charset) - 1);
        text.append(QChar(s_charset[idx]));
    }
    return text;
}

} // anonymous namespace

CaptchaChallenge CaptchaGenerator::generate()
{
    // Generate 5-6 character random text
    QByteArray lenByte = SecureRandom::generate(1);
    int textLength = 5 + (static_cast<unsigned char>(lenByte[0]) % 2); // 5 or 6
    QString answerText = generateRandomText(textLength);

    // Compute SHA-256 hash of the lowercase trimmed answer
    QString normalized = answerText.toLower().trimmed();
    QString answerHash = HashChain::computeSha256(normalized.toUtf8());

    // Render the CAPTCHA image
    const int imgWidth = 200;
    const int imgHeight = 80;
    QImage image(imgWidth, imgHeight, QImage::Format_RGB32);
    image.fill(QColor(245, 245, 245));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw noise lines
    QByteArray noiseBytes = SecureRandom::generate(40);
    QPen noisePen;
    noisePen.setWidth(1);
    for (int i = 0; i < 8; ++i) {
        int x1 = static_cast<unsigned char>(noiseBytes[i * 5]) % imgWidth;
        int y1 = static_cast<unsigned char>(noiseBytes[i * 5 + 1]) % imgHeight;
        int x2 = static_cast<unsigned char>(noiseBytes[i * 5 + 2]) % imgWidth;
        int y2 = static_cast<unsigned char>(noiseBytes[i * 5 + 3]) % imgHeight;
        int colorVal = static_cast<unsigned char>(noiseBytes[i * 5 + 4]);
        noisePen.setColor(QColor(colorVal % 200, (colorVal * 3) % 200, (colorVal * 7) % 200));
        painter.setPen(noisePen);
        painter.drawLine(x1, y1, x2, y2);
    }

    // Draw each character with slight rotation and offset
    QFont font(QStringLiteral("Courier"), 28, QFont::Bold);
    painter.setFont(font);

    QByteArray charNoise = SecureRandom::generate(textLength * 3);
    int charSpacing = (imgWidth - 20) / textLength;

    for (int i = 0; i < textLength; ++i) {
        int xOffset = 10 + i * charSpacing;
        int yBase = imgHeight / 2 + 10;
        int yJitter = (static_cast<unsigned char>(charNoise[i * 3]) % 16) - 8;
        double rotation = (static_cast<unsigned char>(charNoise[i * 3 + 1]) % 30) - 15.0;
        int colorBase = static_cast<unsigned char>(charNoise[i * 3 + 2]);

        painter.save();
        painter.translate(xOffset + charSpacing / 2, yBase + yJitter);
        painter.rotate(rotation);
        painter.setPen(QColor(colorBase % 100, (colorBase * 3) % 100, (colorBase * 7) % 100));
        painter.drawText(-charSpacing / 2, 0, QString(answerText[i]));
        painter.restore();
    }

    painter.end();

    CaptchaChallenge challenge;
    challenge.challengeId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    challenge.image = std::move(image);
    challenge.answerText = answerText;
    challenge.answerHashHex = answerHash;

    return challenge;
}

bool CaptchaGenerator::verifyAnswer(const QString& submittedAnswer,
                                     const QString& storedHashHex)
{
    QString normalized = submittedAnswer.toLower().trimmed();
    QString submittedHash = HashChain::computeSha256(normalized.toUtf8());
    return submittedHash == storedHashHex;
}
