#pragma once
// CaptchaGenerator.h — ProctorOps
// Locally rendered CAPTCHA image generation for the login flow.
// Generates a random alphanumeric string, renders it as a distorted QImage,
// and returns the SHA-256 hash of the answer for storage in CaptchaState.

#include <QImage>
#include <QString>

struct CaptchaChallenge {
    QString challengeId;   // UUID for this challenge
    QImage  image;         // rendered CAPTCHA image (to display in CaptchaDialog)
    QString answerText;    // plaintext answer (for hashing before storage; never persisted)
    QString answerHashHex; // SHA-256 hex of lowercase trimmed answer
};

class CaptchaGenerator {
public:
    CaptchaGenerator() = delete;

    /// Generate a new CAPTCHA challenge with a random alphanumeric string.
    [[nodiscard]] static CaptchaChallenge generate();

    /// Verify a user-submitted answer against a stored SHA-256 answer hash.
    [[nodiscard]] static bool verifyAnswer(const QString& submittedAnswer,
                                            const QString& storedHashHex);
};
