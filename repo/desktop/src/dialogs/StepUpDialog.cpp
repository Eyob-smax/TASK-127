// StepUpDialog.cpp — ProctorOps

#include "dialogs/StepUpDialog.h"
#include "services/AuthService.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>

StepUpDialog::StepUpDialog(AuthService&   authService,
                            const QString& sessionToken,
                            const QString& actionDescription,
                            QWidget*       parent)
    : QDialog(parent)
    , m_authService(authService)
    , m_sessionToken(sessionToken)
{
    setWindowTitle(QStringLiteral("Re-authentication Required"));
    setMinimumWidth(380);
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    // Description
    auto* descLabel = new QLabel(
        QStringLiteral("Step-up verification required to:\n") + actionDescription, this);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // Password field
    auto* pwLabel = new QLabel(QStringLiteral("Enter your password:"), this);
    layout->addWidget(pwLabel);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("Password"));
    layout->addWidget(m_passwordEdit);

    // Error label (hidden until needed)
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    // Buttons
    auto* buttons = new QDialogButtonBox(this);
    auto* confirmBtn = buttons->addButton(QStringLiteral("Confirm"),
                                          QDialogButtonBox::AcceptRole);
    auto* cancelBtn  = buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(confirmBtn, &QPushButton::clicked, this, &StepUpDialog::onConfirmClicked);
    connect(cancelBtn,  &QPushButton::clicked, this, &QDialog::reject);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &StepUpDialog::onConfirmClicked);

    m_passwordEdit->setFocus();
}

void StepUpDialog::onConfirmClicked()
{
    const QString password = m_passwordEdit->text();
    if (password.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("Password is required."));
        m_errorLabel->show();
        return;
    }

    auto result = m_authService.initiateStepUp(m_sessionToken, password);
    if (!result.isOk()) {
        m_errorLabel->setText(QStringLiteral("Authentication failed. Check your password and try again."));
        m_errorLabel->show();
        m_passwordEdit->clear();
        m_passwordEdit->setFocus();
        return;
    }

    m_stepUpWindowId = result.value().id;
    accept();
}
