// SecurityAdminWindow.cpp - ProctorOps

#include "SecurityAdminWindow.h"

#include "app/AppContext.h"
#include "dialogs/StepUpDialog.h"
#include "models/Audit.h"
#include "models/CommonTypes.h"
#include "services/AuditService.h"
#include "services/AuthService.h"
#include "services/CheckInService.h"

#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace {

QString userStatusLabel(UserStatus status)
{
    switch (status) {
    case UserStatus::Active:
        return QStringLiteral("Active");
    case UserStatus::Locked:
        return QStringLiteral("Locked");
    case UserStatus::Deactivated:
        return QStringLiteral("Deactivated");
    }
    return QStringLiteral("Unknown");
}

QString roleSelectionLabel(Role role)
{
    return roleToString(role);
}

Role roleFromSelection(const QString& label)
{
    if (label == QStringLiteral("PROCTOR"))
        return Role::Proctor;
    if (label == QStringLiteral("CONTENT_MANAGER"))
        return Role::ContentManager;
    if (label == QStringLiteral("SECURITY_ADMINISTRATOR"))
        return Role::SecurityAdministrator;
    return Role::FrontDeskOperator;
}

}

SecurityAdminWindow::SecurityAdminWindow(AppContext& ctx, QWidget* parent)
    : QWidget(parent)
    , m_ctx(ctx)
{
    setWindowTitle(tr("Security Administration"));
    setupUi();

    QTimer::singleShot(0, this, &SecurityAdminWindow::onRefreshUsers);
    QTimer::singleShot(0, this, &SecurityAdminWindow::onRefreshFreezes);
    QTimer::singleShot(0, this, &SecurityAdminWindow::onRefreshPrivilegedAudit);
}

void SecurityAdminWindow::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    auto* header = new QLabel(
        tr("<b>Security Administration</b> - "
           "Role management, account freeze/thaw, and privileged audit inspection. "
           "All actions require SecurityAdministrator role and step-up re-authentication."),
        this);
    header->setWordWrap(true);
    mainLayout->addWidget(header);

    auto* tabs = new QTabWidget(this);
    auto* usersTab = new QWidget();
    auto* freezeTab = new QWidget();
    auto* auditTab = new QWidget();

    setupUsersTab(usersTab);
    setupFreezeTab(freezeTab);
    setupPrivilegedAuditTab(auditTab);

    tabs->addTab(usersTab, tr("User Roles"));
    tabs->addTab(freezeTab, tr("Account Freezes"));
    tabs->addTab(auditTab, tr("Privileged Audit"));

    mainLayout->addWidget(tabs);
}

void SecurityAdminWindow::setupUsersTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    m_usersTable = new QTableWidget(0, 4, tab);
    m_usersTable->setHorizontalHeaderLabels({
        tr("Username"), tr("Role"), tr("Status"), tr("User ID")
    });
    m_usersTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_usersTable->setColumnHidden(3, true);
    m_usersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_usersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_usersTable->setAlternatingRowColors(true);
    connect(m_usersTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const bool selected = m_usersTable->currentRow() >= 0;
        m_resetPasswordBtn->setEnabled(selected);
        m_changeRoleBtn->setEnabled(selected);
        m_unlockUserBtn->setEnabled(selected);
        m_deactivateUserBtn->setEnabled(selected);
    });
    layout->addWidget(m_usersTable);

    auto* buttonRow = new QHBoxLayout();
    m_createUserBtn = new QPushButton(tr("Create User..."), tab);
    m_resetPasswordBtn = new QPushButton(tr("Reset Password..."), tab);
    m_changeRoleBtn = new QPushButton(tr("Change Role..."), tab);
    m_unlockUserBtn = new QPushButton(tr("Unlock Account"), tab);
    m_deactivateUserBtn = new QPushButton(tr("Deactivate User"), tab);
    auto* refreshBtn = new QPushButton(tr("Refresh"), tab);

    m_resetPasswordBtn->setEnabled(false);
    m_changeRoleBtn->setEnabled(false);
    m_unlockUserBtn->setEnabled(false);
    m_deactivateUserBtn->setEnabled(false);

    connect(m_createUserBtn, &QPushButton::clicked, this, &SecurityAdminWindow::onCreateUser);
    connect(m_resetPasswordBtn, &QPushButton::clicked, this, &SecurityAdminWindow::onResetUserPassword);
    connect(m_changeRoleBtn, &QPushButton::clicked, this, &SecurityAdminWindow::onChangeRole);
    connect(m_unlockUserBtn, &QPushButton::clicked, this, &SecurityAdminWindow::onUnlockUser);
    connect(m_deactivateUserBtn, &QPushButton::clicked, this, &SecurityAdminWindow::onDeactivateUser);
    connect(refreshBtn, &QPushButton::clicked, this, &SecurityAdminWindow::onRefreshUsers);

    buttonRow->addWidget(m_createUserBtn);
    buttonRow->addWidget(m_resetPasswordBtn);
    buttonRow->addWidget(m_changeRoleBtn);
    buttonRow->addWidget(m_unlockUserBtn);
    buttonRow->addWidget(m_deactivateUserBtn);
    buttonRow->addStretch();
    buttonRow->addWidget(refreshBtn);
    layout->addLayout(buttonRow);
}

void SecurityAdminWindow::setupFreezeTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    auto* note = new QLabel(
        tr("Freezing an account blocks all future check-ins for the member until thawed. "
           "Freeze and thaw operations are audited and require step-up."),
        tab);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* form = new QGroupBox(tr("Apply Freeze"), tab);
    auto* formLayout = new QFormLayout(form);

    m_memberIdInput = new QLineEdit(tab);
    m_freezeReasonInput = new QLineEdit(tab);
    m_memberIdInput->setPlaceholderText(tr("Member ID"));
    m_freezeReasonInput->setPlaceholderText(tr("Reason for freeze"));

    formLayout->addRow(tr("Member ID:"), m_memberIdInput);
    formLayout->addRow(tr("Reason:"), m_freezeReasonInput);

    m_freezeBtn = new QPushButton(tr("Freeze Account (step-up required)"), tab);
    m_thawBtn = new QPushButton(tr("Thaw Account (step-up required)"), tab);
    connect(m_freezeBtn, &QPushButton::clicked, this, &SecurityAdminWindow::onFreezeAccount);
    connect(m_thawBtn, &QPushButton::clicked, this, &SecurityAdminWindow::onThawAccount);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(m_freezeBtn);
    buttonRow->addWidget(m_thawBtn);
    formLayout->addRow(buttonRow);

    layout->addWidget(form);

    auto* historyLabel = new QLabel(tr("Recent Freezes:"), tab);
    layout->addWidget(historyLabel);

    m_freezeTable = new QTableWidget(0, 4, tab);
    m_freezeTable->setHorizontalHeaderLabels({
        tr("Member ID"), tr("Reason"), tr("Frozen At"), tr("Thawed At")
    });
    m_freezeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_freezeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_freezeTable->setAlternatingRowColors(true);
    layout->addWidget(m_freezeTable);

    auto* refreshBtn = new QPushButton(tr("Refresh"), tab);
    connect(refreshBtn, &QPushButton::clicked, this, &SecurityAdminWindow::onRefreshFreezes);
    layout->addWidget(refreshBtn, 0, Qt::AlignRight);
}

void SecurityAdminWindow::setupPrivilegedAuditTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    auto* note = new QLabel(
        tr("Shows audit events for privileged actions: role changes, key revocations, "
           "freeze/thaw, step-up verifications, and export/deletion completions."),
        tab);
    note->setWordWrap(true);
    layout->addWidget(note);

    m_auditTable = new QTableWidget(0, 5, tab);
    m_auditTable->setHorizontalHeaderLabels({
        tr("Timestamp"), tr("Actor"), tr("Event"), tr("Entity"), tr("Entity ID")
    });
    m_auditTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_auditTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_auditTable->setAlternatingRowColors(true);
    layout->addWidget(m_auditTable);

    m_refreshAuditBtn = new QPushButton(tr("Refresh"), tab);
    connect(m_refreshAuditBtn, &QPushButton::clicked,
            this, &SecurityAdminWindow::onRefreshPrivilegedAudit);
    layout->addWidget(m_refreshAuditBtn, 0, Qt::AlignRight);
}

void SecurityAdminWindow::onCreateUser()
{
    if (!m_ctx.authService)
        return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token, tr("Create local user"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    bool usernameOk = false;
    const QString username = QInputDialog::getText(this,
                                                   tr("Create User"),
                                                   tr("Username:"),
                                                   QLineEdit::Normal,
                                                   QString(),
                                                   &usernameOk).trimmed();
    if (!usernameOk)
        return;

    bool roleOk = false;
    const QString selectedRole = QInputDialog::getItem(
        this,
        tr("Create User"),
        tr("Role:"),
        {
            roleSelectionLabel(Role::FrontDeskOperator),
            roleSelectionLabel(Role::Proctor),
            roleSelectionLabel(Role::ContentManager),
            roleSelectionLabel(Role::SecurityAdministrator)
        },
        0,
        false,
        &roleOk);
    if (!roleOk)
        return;

    bool passwordOk = false;
    const QString password = QInputDialog::getText(this,
                                                   tr("Create User"),
                                                   tr("Temporary password:"),
                                                   QLineEdit::Password,
                                                   QString(),
                                                   &passwordOk);
    if (!passwordOk)
        return;

    bool confirmOk = false;
    const QString confirmPassword = QInputDialog::getText(this,
                                                          tr("Create User"),
                                                          tr("Confirm password:"),
                                                          QLineEdit::Password,
                                                          QString(),
                                                          &confirmOk);
    if (!confirmOk)
        return;

    if (password != confirmPassword) {
        QMessageBox::warning(this, tr("Validation"), tr("Passwords do not match."));
        return;
    }

    auto result = m_ctx.authService->createUser(m_ctx.session.userId,
                                                username,
                                                password,
                                                roleFromSelection(selectedRole),
                                                dlg.stepUpWindowId());
    if (!result.isOk()) {
        QMessageBox::critical(this, tr("Error"), result.errorMessage());
        return;
    }

    onRefreshUsers();
}

void SecurityAdminWindow::onResetUserPassword()
{
    const int row = m_usersTable->currentRow();
    if (row < 0 || !m_ctx.authService)
        return;

    const QString targetUserId = m_usersTable->item(row, 3)->text();
    const QString targetUsername = m_usersTable->item(row, 0)->text();

    bool passwordOk = false;
    const QString newPassword = QInputDialog::getText(this,
                                                      tr("Reset Password"),
                                                      tr("New password for %1:").arg(targetUsername),
                                                      QLineEdit::Password,
                                                      QString(),
                                                      &passwordOk);
    if (!passwordOk)
        return;

    bool confirmOk = false;
    const QString confirmPassword = QInputDialog::getText(this,
                                                          tr("Reset Password"),
                                                          tr("Confirm new password:"),
                                                          QLineEdit::Password,
                                                          QString(),
                                                          &confirmOk);
    if (!confirmOk)
        return;

    if (newPassword != confirmPassword) {
        QMessageBox::warning(this, tr("Validation"), tr("Passwords do not match."));
        return;
    }

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token, tr("Reset user password"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    auto result = m_ctx.authService->resetUserPassword(m_ctx.session.userId,
                                                       targetUserId,
                                                       newPassword,
                                                       dlg.stepUpWindowId());
    if (!result.isOk()) {
        QMessageBox::critical(this, tr("Error"), result.errorMessage());
        return;
    }

    QMessageBox::information(this,
                             tr("Password Reset"),
                             tr("Password reset completed for user '%1'.").arg(targetUsername));
}

void SecurityAdminWindow::onChangeRole()
{
    const int row = m_usersTable->currentRow();
    if (row < 0 || !m_ctx.authService)
        return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token, tr("Change user role"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString userId = m_usersTable->item(row, 3)->text();
    bool ok = false;
    const QString selectedRole = QInputDialog::getItem(
        this,
        tr("Change Role"),
        tr("Select new role for user:"),
        {
            roleSelectionLabel(Role::FrontDeskOperator),
            roleSelectionLabel(Role::Proctor),
            roleSelectionLabel(Role::ContentManager),
            roleSelectionLabel(Role::SecurityAdministrator)
        },
        0,
        false,
        &ok);
    if (!ok)
        return;

    auto result = m_ctx.authService->changeUserRole(m_ctx.session.userId,
                                                    userId,
                                                    roleFromSelection(selectedRole),
                                                    dlg.stepUpWindowId());
    if (!result.isOk()) {
        QMessageBox::critical(this, tr("Error"), result.errorMessage());
        return;
    }

    onRefreshUsers();
}

void SecurityAdminWindow::onUnlockUser()
{
    const int row = m_usersTable->currentRow();
    if (row < 0 || !m_ctx.authService)
        return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token, tr("Unlock user account"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString userId = m_usersTable->item(row, 3)->text();
    auto result = m_ctx.authService->unlockUser(m_ctx.session.userId, userId, dlg.stepUpWindowId());
    if (!result.isOk()) {
        QMessageBox::critical(this, tr("Error"), result.errorMessage());
        return;
    }

    onRefreshUsers();
}

void SecurityAdminWindow::onDeactivateUser()
{
    const int row = m_usersTable->currentRow();
    if (row < 0 || !m_ctx.authService)
        return;

    if (QMessageBox::warning(this,
                             tr("Deactivate User"),
                             tr("This will prevent the user from signing in. Continue?"),
                             QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token, tr("Deactivate user account"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString userId = m_usersTable->item(row, 3)->text();
    auto result = m_ctx.authService->deactivateUser(m_ctx.session.userId, userId, dlg.stepUpWindowId());
    if (!result.isOk()) {
        QMessageBox::critical(this, tr("Error"), result.errorMessage());
        return;
    }

    onRefreshUsers();
}

void SecurityAdminWindow::onRefreshUsers()
{
    m_usersTable->setRowCount(0);
    if (!m_ctx.authService)
        return;

    auto result = m_ctx.authService->listUsers(m_ctx.session.userId);
    if (!result.isOk())
        return;

    for (const User& user : result.value()) {
        const int row = m_usersTable->rowCount();
        m_usersTable->insertRow(row);
        m_usersTable->setItem(row, 0, new QTableWidgetItem(user.username));
        m_usersTable->setItem(row, 1, new QTableWidgetItem(roleToString(user.role)));
        m_usersTable->setItem(row, 2, new QTableWidgetItem(userStatusLabel(user.status)));
        m_usersTable->setItem(row, 3, new QTableWidgetItem(user.id));
    }
}

void SecurityAdminWindow::onFreezeAccount()
{
    if (!m_ctx.authService || !m_ctx.checkInService)
        return;

    const QString memberHumanId = m_memberIdInput->text().trimmed();
    const QString reason = m_freezeReasonInput->text().trimmed();
    if (memberHumanId.isEmpty() || reason.isEmpty()) {
        QMessageBox::warning(this, tr("Validation"), tr("Member ID and reason are required."));
        return;
    }

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token, tr("Freeze member account"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    auto freezeResult = m_ctx.checkInService->freezeMemberAccount(
        memberHumanId,
        reason,
        m_ctx.session.userId,
        dlg.stepUpWindowId());
    if (!freezeResult.isOk()) {
        QMessageBox::critical(this, tr("Error"), freezeResult.errorMessage());
        return;
    }

    m_memberIdInput->clear();
    m_freezeReasonInput->clear();
    onRefreshFreezes();
}

void SecurityAdminWindow::onThawAccount()
{
    if (!m_ctx.authService || !m_ctx.checkInService)
        return;

    const QString memberHumanId = m_memberIdInput->text().trimmed();
    if (memberHumanId.isEmpty()) {
        QMessageBox::warning(this, tr("Validation"), tr("Enter the Member ID to thaw."));
        return;
    }

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token, tr("Thaw member account"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    auto thawResult = m_ctx.checkInService->thawMemberAccount(
        memberHumanId,
        m_ctx.session.userId,
        dlg.stepUpWindowId());
    if (!thawResult.isOk()) {
        QMessageBox::critical(this, tr("Error"), thawResult.errorMessage());
        return;
    }

    m_memberIdInput->clear();
    onRefreshFreezes();
}

void SecurityAdminWindow::onRefreshFreezes()
{
    m_freezeTable->setRowCount(0);
    if (!m_ctx.authService || !m_ctx.checkInService)
        return;

    auto authResult = m_ctx.authService->requireRoleForActor(
        m_ctx.session.userId,
        Role::SecurityAdministrator);
    if (authResult.isErr())
        return;

    auto recordsResult = m_ctx.checkInService->listRecentFreezeRecords(
        m_ctx.session.userId,
        50);
    if (!recordsResult.isOk())
        return;

    for (const FreezeRecordView& record : recordsResult.value()) {
        const int row = m_freezeTable->rowCount();
        m_freezeTable->insertRow(row);
        m_freezeTable->setItem(row, 0, new QTableWidgetItem(record.memberId));
        m_freezeTable->setItem(row, 1, new QTableWidgetItem(record.reason));
        m_freezeTable->setItem(row, 2, new QTableWidgetItem(record.frozenAt.toString(Qt::ISODate)));
        m_freezeTable->setItem(row, 3, new QTableWidgetItem(
            record.thawedAt.isNull() ? tr("Active") : record.thawedAt.toString(Qt::ISODate)));
    }
}

void SecurityAdminWindow::onRefreshPrivilegedAudit()
{
    m_auditTable->setRowCount(0);
    if (!m_ctx.authService || !m_ctx.auditService)
        return;

    auto authResult = m_ctx.authService->requireRoleForActor(
        m_ctx.session.userId,
        Role::SecurityAdministrator);
    if (authResult.isErr())
        return;

    AuditFilter filter;
    filter.fromTimestamp = QDateTime::currentDateTimeUtc().addDays(-30);
    filter.limit = 200;
    filter.offset = 0;

    auto result = m_ctx.auditService->queryEvents(m_ctx.session.userId, filter);
    if (!result.isOk())
        return;

    const QSet<QString> privilegedEvents = {
        QStringLiteral("ROLE_CHANGED"),
        QStringLiteral("USER_DEACTIVATED"),
        QStringLiteral("USER_UNLOCKED"),
        QStringLiteral("PASSWORD_RESET"),
        QStringLiteral("KEY_IMPORTED"),
        QStringLiteral("KEY_REVOKED"),
        QStringLiteral("STEP_UP_INITIATED"),
        QStringLiteral("STEP_UP_PASSED"),
        QStringLiteral("EXPORT_COMPLETED"),
        QStringLiteral("DELETION_COMPLETED"),
        QStringLiteral("MEMBER_FREEZE_APPLIED"),
        QStringLiteral("MEMBER_FREEZE_THAWED"),
        QStringLiteral("UPDATE_APPLIED"),
        QStringLiteral("UPDATE_ROLLED_BACK"),
        QStringLiteral("SYNC_CONFLICT_RESOLVED")
    };

    for (const AuditEntry& entry : result.value()) {
        const QString eventString = auditEventTypeToString(entry.eventType);
        if (!privilegedEvents.contains(eventString))
            continue;

        const int row = m_auditTable->rowCount();
        m_auditTable->insertRow(row);
        m_auditTable->setItem(row, 0, new QTableWidgetItem(entry.timestamp.toString(Qt::ISODate)));
        m_auditTable->setItem(row, 1, new QTableWidgetItem(entry.actorUserId));
        m_auditTable->setItem(row, 2, new QTableWidgetItem(eventString));
        m_auditTable->setItem(row, 3, new QTableWidgetItem(entry.entityType));
        m_auditTable->setItem(row, 4, new QTableWidgetItem(entry.entityId));
    }
}
