#pragma once
// SecurityAdminWindow.h — ProctorOps
// Security administration window.
//
// Provides:
//   Role & user management — view users, change roles (SecurityAdministrator + step-up)
//   Freeze controls — apply/thaw member account freezes (SecurityAdministrator + step-up)
//   Privileged audit view — filter audit log for security-relevant events
//   Console lock — shortcut to Ctrl+L behavior
//
// All privileged actions are audited. RBAC is enforced at the service layer.

#include <QWidget>

class QTableWidget;
class QTabWidget;
class QPushButton;
class QLabel;
class QComboBox;
class QLineEdit;
class AppContext;

class SecurityAdminWindow : public QWidget {
    Q_OBJECT
public:
    static constexpr const char* WindowId = "window.security_admin";

    explicit SecurityAdminWindow(AppContext& ctx, QWidget* parent = nullptr);

private slots:
    // Users tab
    void onCreateUser();
    void onResetUserPassword();
    void onChangeRole();
    void onUnlockUser();
    void onDeactivateUser();
    void onRefreshUsers();

    // Freeze tab
    void onFreezeAccount();
    void onThawAccount();
    void onRefreshFreezes();

    // Privileged audit tab
    void onRefreshPrivilegedAudit();

private:
    void setupUi();
    void setupUsersTab(QWidget* tab);
    void setupFreezeTab(QWidget* tab);
    void setupPrivilegedAuditTab(QWidget* tab);

    AppContext&    m_ctx;

    // Users tab
    QTableWidget*  m_usersTable{nullptr};
    QPushButton*   m_createUserBtn{nullptr};
    QPushButton*   m_resetPasswordBtn{nullptr};
    QPushButton*   m_changeRoleBtn{nullptr};
    QPushButton*   m_unlockUserBtn{nullptr};
    QPushButton*   m_deactivateUserBtn{nullptr};

    // Freeze tab
    QLineEdit*     m_memberIdInput{nullptr};
    QLineEdit*     m_freezeReasonInput{nullptr};
    QPushButton*   m_freezeBtn{nullptr};
    QPushButton*   m_thawBtn{nullptr};
    QTableWidget*  m_freezeTable{nullptr};

    // Audit tab
    QTableWidget*  m_auditTable{nullptr};
    QPushButton*   m_refreshAuditBtn{nullptr};
};
