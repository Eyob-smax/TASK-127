#pragma once
// StepUpDialog.h — ProctorOps
// Re-authentication dialog for sensitive operations.
//
// Presents a password prompt to the current operator and calls
// AuthService::initiateStepUp(). On success the StepUpWindow id is available
// via stepUpWindowId() and the dialog result is QDialog::Accepted.
//
// Usage:
//   StepUpDialog dlg(ctx.authService, ctx.session.token, "Approve correction", this);
//   if (dlg.exec() == QDialog::Accepted)
//       checkInService.approveCorrection(reqId, rationale, userId, dlg.stepUpWindowId());

#include <QDialog>

class QLineEdit;
class QLabel;
class AuthService;

class StepUpDialog : public QDialog {
    Q_OBJECT

public:
    explicit StepUpDialog(AuthService&    authService,
                          const QString&  sessionToken,
                          const QString&  actionDescription,
                          QWidget*        parent = nullptr);

    /// The StepUpWindow id granted by AuthService. Empty if dialog was cancelled
    /// or authentication failed.
    [[nodiscard]] QString stepUpWindowId() const { return m_stepUpWindowId; }

private slots:
    void onConfirmClicked();

private:
    AuthService& m_authService;
    QString      m_sessionToken;
    QString      m_stepUpWindowId;

    QLineEdit*   m_passwordEdit{nullptr};
    QLabel*      m_errorLabel{nullptr};
};
