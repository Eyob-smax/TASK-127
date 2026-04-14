// LoginWindow.cpp — ProctorOps

#include "windows/LoginWindow.h"
#include "services/AuthService.h"
#include "utils/ErrorFormatter.h"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPixmap>

LoginWindow::LoginWindow(AuthService& authService, QWidget* parent)
    : QWidget(parent)
    , m_authService(authService)
{
    setupUi();
}

void LoginWindow::setupUi()
{
    setWindowTitle(tr("ProctorOps — Sign In"));
    setMinimumSize(420, 340);
    setMaximumWidth(540);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(40, 36, 40, 36);
    root->setSpacing(10);

    m_titleLabel = new QLabel(tr("ProctorOps"), this);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 22px; font-weight: bold; color: #1a1a2e;"));
    m_titleLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_titleLabel);

    root->addSpacing(12);

    // Username / password form
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText(tr("Username"));
    m_usernameEdit->setMaxLength(128);
    form->addRow(tr("Username:"), m_usernameEdit);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setPlaceholderText(tr("Password"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Password:"), m_passwordEdit);

    root->addLayout(form);

    // CAPTCHA section — hidden until three failures
    m_captchaFrame = new QFrame(this);
    m_captchaFrame->setVisible(false);
    m_captchaFrame->setFrameShape(QFrame::Box);
    m_captchaFrame->setLineWidth(1);

    auto* captchaLayout = new QVBoxLayout(m_captchaFrame);

    m_captchaImage = new QLabel(m_captchaFrame);
    m_captchaImage->setAlignment(Qt::AlignCenter);
    m_captchaImage->setMinimumHeight(64);
    captchaLayout->addWidget(m_captchaImage);

    auto* captchaRow = new QHBoxLayout();
    m_captchaEdit = new QLineEdit(m_captchaFrame);
    m_captchaEdit->setPlaceholderText(tr("Enter the text shown above"));
    m_captchaEdit->setMaxLength(10);
    captchaRow->addWidget(m_captchaEdit, 1);

    m_refreshCaptchaBtn = new QPushButton(tr("Refresh"), m_captchaFrame);
    m_refreshCaptchaBtn->setToolTip(tr("Generate a new CAPTCHA"));
    captchaRow->addWidget(m_refreshCaptchaBtn);
    captchaLayout->addLayout(captchaRow);

    root->addWidget(m_captchaFrame);

    // Feedback label (errors and info messages)
    m_feedbackLabel = new QLabel(this);
    m_feedbackLabel->setWordWrap(true);
    m_feedbackLabel->setVisible(false);
    root->addWidget(m_feedbackLabel);

    // Sign-in button
    m_signInBtn = new QPushButton(tr("Sign In"), this);
    m_signInBtn->setDefault(true);
    m_signInBtn->setMinimumHeight(36);
    root->addWidget(m_signInBtn);

    root->addStretch();

    // Connections
    connect(m_signInBtn,         &QPushButton::clicked,
            this, &LoginWindow::onSignInClicked);
    connect(m_refreshCaptchaBtn, &QPushButton::clicked,
            this, &LoginWindow::onRefreshCaptchaClicked);
    connect(m_passwordEdit, &QLineEdit::returnPressed,
            this, &LoginWindow::onSignInClicked);
    connect(m_captchaEdit,  &QLineEdit::returnPressed,
            this, &LoginWindow::onSignInClicked);
    connect(m_usernameEdit, &QLineEdit::textChanged,
            this, [this]{ clearFeedback(); });
    connect(m_passwordEdit, &QLineEdit::textChanged,
            this, [this]{ clearFeedback(); });
}

void LoginWindow::checkBootstrapMode()
{
    if (!m_authService.hasAnySecurityAdministrator())
        enterBootstrapMode();
}

void LoginWindow::enterBootstrapMode()
{
    m_bootstrapMode = true;
    m_titleLabel->setText(tr("ProctorOps — Initial Setup"));
    m_signInBtn->setText(tr("Create Administrator Account"));
    showInfo(tr("No administrator accounts exist. "
                "Create the first security administrator to begin."));
}

void LoginWindow::onSignInClicked()
{
    if (m_bootstrapMode) {
        const QString username = m_usernameEdit->text().trimmed();
        const QString password = m_passwordEdit->text();

        if (username.isEmpty() || password.isEmpty()) {
            showError(tr("Username and password are required."));
            return;
        }

        auto bootstrapResult =
            m_authService.bootstrapSecurityAdministrator(username, password);
        if (bootstrapResult.isErr()) {
            showError(ErrorFormatter::toUserMessage(bootstrapResult.errorCode()));
            return;
        }

        const UserSession& session = bootstrapResult.value();
        emit loginSucceeded(session.token, session.userId);
        return;
    }

    clearFeedback();

    const QString username = m_usernameEdit->text().trimmed();
    const QString password = m_passwordEdit->text();
    const QString captcha  = m_captchaEdit->text().trimmed();

    if (username.isEmpty() || password.isEmpty()) {
        showError(tr("Username and password are required."));
        return;
    }

    auto result = m_authService.signIn(username, password, captcha);

    if (result.isOk()) {
        const UserSession& session = result.value();
        emit loginSucceeded(session.token, session.userId);
        return;
    }

    const ErrorCode code = result.errorCode();
    showError(ErrorFormatter::toUserMessage(code));

    if (code == ErrorCode::CaptchaRequired && !m_captchaVisible) {
        refreshCaptchaImage();
        showCaptchaSection(true);
    }
}

void LoginWindow::onRefreshCaptchaClicked()
{
    refreshCaptchaImage();
}

void LoginWindow::refreshCaptchaImage()
{
    const QString username = m_usernameEdit->text().trimmed();
    if (username.isEmpty()) return;

    auto result = m_authService.refreshCaptcha(username);
    if (result.isOk()) {
        m_captchaImage->setPixmap(
            QPixmap::fromImage(result.value().image).scaledToHeight(64));
        m_captchaEdit->clear();
        m_captchaEdit->setFocus();
    } else {
        showError(tr("Could not generate CAPTCHA. Please try again."));
    }
}

void LoginWindow::showCaptchaSection(bool visible)
{
    m_captchaVisible = visible;
    m_captchaFrame->setVisible(visible);
}

void LoginWindow::showError(const QString& message)
{
    m_feedbackLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_feedbackLabel->setText(message);
    m_feedbackLabel->setVisible(true);
}

void LoginWindow::showInfo(const QString& message)
{
    m_feedbackLabel->setStyleSheet(QStringLiteral("color: #2471a3;"));
    m_feedbackLabel->setText(message);
    m_feedbackLabel->setVisible(true);
}

void LoginWindow::clearFeedback()
{
    m_feedbackLabel->setVisible(false);
}
