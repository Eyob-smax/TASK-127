#pragma once
// LoginWindow.h — ProctorOps
// Authentication window. Presents sign-in form, lockout feedback, locally
// rendered CAPTCHA after three failures, and bootstrap detection (first-admin
// setup mode when no security administrator accounts exist).
//
// This window does not manage sessions — it emits loginSucceeded() and lets the
// Application shell own the session token and routing decisions.

#include <QWidget>
#include <QString>

class QLineEdit;
class QPushButton;
class QLabel;
class QFrame;
class AuthService;

class LoginWindow : public QWidget {
    Q_OBJECT

public:
    explicit LoginWindow(AuthService& authService, QWidget* parent = nullptr);

    /// Call once the window is shown to detect first-run bootstrap mode.
    void checkBootstrapMode();

signals:
    /// Emitted on successful sign-in. Carries the active session token.
    void loginSucceeded(const QString& sessionToken, const QString& userId);

private slots:
    void onSignInClicked();
    void onRefreshCaptchaClicked();

private:
    void setupUi();
    void showCaptchaSection(bool visible);
    void showError(const QString& message);
    void showInfo(const QString& message);
    void clearFeedback();
    void refreshCaptchaImage();
    void enterBootstrapMode();

    AuthService&   m_authService;
    bool           m_bootstrapMode{false};
    bool           m_captchaVisible{false};

    QLabel*        m_titleLabel{nullptr};
    QLineEdit*     m_usernameEdit{nullptr};
    QLineEdit*     m_passwordEdit{nullptr};
    QFrame*        m_captchaFrame{nullptr};
    QLabel*        m_captchaImage{nullptr};
    QLineEdit*     m_captchaEdit{nullptr};
    QPushButton*   m_refreshCaptchaBtn{nullptr};
    QLabel*        m_feedbackLabel{nullptr};
    QPushButton*   m_signInBtn{nullptr};
};
