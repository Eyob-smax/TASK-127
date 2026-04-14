// UpdateService.cpp - ProctorOps

#include "UpdateService.h"

#include "AuthService.h"
#include "utils/Logger.h"
#include "models/CommonTypes.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

namespace {

QString normalizedPath(QString path)
{
    path = QDir::cleanPath(path);
    return QDir::fromNativeSeparators(path);
}

QString updateRuntimeRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/update_runtime");
}

QString liveComponentsDir()
{
    return updateRuntimeRoot() + QStringLiteral("/live");
}

QString historyBackupDir(const QString& historyId)
{
    return updateRuntimeRoot() + QStringLiteral("/history/") + historyId;
}

bool copyFileReplacing(const QString& sourcePath, const QString& targetPath, QString* errorMessage)
{
    QFileInfo targetInfo(targetPath);
    if (!QDir().mkpath(targetInfo.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create destination directory: %1")
                                .arg(targetInfo.absolutePath());
        }
        return false;
    }

    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot replace destination file: %1")
                                .arg(targetPath);
        }
        return false;
    }

    if (!QFile::copy(sourcePath, targetPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot copy '%1' to '%2'")
                                .arg(sourcePath, targetPath);
        }
        return false;
    }

    return true;
}

}

UpdateService::UpdateService(IUpdateRepository& updateRepo,
                             ISyncRepository& syncRepo,
                             AuthService& authService,
                             PackageVerifier& verifier,
                             AuditService& auditService)
    : m_updateRepo(updateRepo)
    , m_syncRepo(syncRepo)
    , m_authService(authService)
    , m_verifier(verifier)
    , m_auditService(auditService)
{
}

Result<UpdatePackage> UpdateService::importPackage(const QString& pkgDir,
                                                   const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::SecurityAdministrator);
    if (authResult.isErr())
        return Result<UpdatePackage>::err(authResult.errorCode(), authResult.errorMessage());

    QFile manifestFile(pkgDir + QStringLiteral("/update-manifest.json"));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        return Result<UpdatePackage>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Cannot open update-manifest.json in: %1").arg(pkgDir));
    }

    const QByteArray manifestBytes = manifestFile.readAll();
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestBytes);
    if (!manifestDoc.isObject()) {
        return Result<UpdatePackage>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("update-manifest.json is not valid JSON"));
    }

    const QJsonObject manifest = manifestDoc.object();
    const QString packageId = manifest[QStringLiteral("package_id")].toString();
    const QString version = manifest[QStringLiteral("version")].toString();
    const QString platform = manifest[QStringLiteral("target_platform")].toString();
    const QString description = manifest[QStringLiteral("description")].toString();
    const QString signerKeyId = manifest[QStringLiteral("signer_key_id")].toString();
    const QString signatureHex = manifest[QStringLiteral("signature")].toString();
    if (packageId.isEmpty() || version.isEmpty() || signerKeyId.isEmpty() || signatureHex.isEmpty()) {
        return Result<UpdatePackage>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("update-manifest.json missing required fields"));
    }

    QJsonObject manifestBody = manifest;
    manifestBody.remove(QStringLiteral("signature"));
    const QByteArray manifestBodyBytes = QJsonDocument(manifestBody).toJson(QJsonDocument::Compact);
    const QByteArray signatureBytes = QByteArray::fromHex(signatureHex.toLatin1());
    auto signatureResult = m_verifier.verifyPackageSignature(manifestBodyBytes, signatureBytes, signerKeyId);

    UpdatePackage pkg;
    pkg.id = packageId;
    pkg.version = version;
    pkg.targetPlatform = platform;
    pkg.description = description;
    pkg.signerKeyId = signerKeyId;
    pkg.signatureValid = signatureResult.isOk() && signatureResult.value();
    pkg.stagedPath = pkgDir;
    pkg.status = pkg.signatureValid ? UpdatePackageStatus::Staged : UpdatePackageStatus::Rejected;
    pkg.importedAt = QDateTime::currentDateTimeUtc();
    pkg.importedByUserId = actorUserId;

    auto insertResult = m_updateRepo.insertPackage(pkg);
    if (!insertResult.isOk())
        return Result<UpdatePackage>::err(insertResult.errorCode(), insertResult.errorMessage());

    if (!pkg.signatureValid) {
        m_auditService.recordEvent(actorUserId,
                                   AuditEventType::UpdateImported,
                                   QStringLiteral("UpdatePackage"), packageId,
                                   {}, {
                                       {QStringLiteral("version"), version},
                                       {QStringLiteral("status"), QStringLiteral("Rejected")},
                                       {QStringLiteral("reason"), QStringLiteral("signature_invalid")}
                                   });
        return Result<UpdatePackage>::err(
            signatureResult.isOk() ? ErrorCode::SignatureInvalid : signatureResult.errorCode(),
            signatureResult.isOk()
                ? QStringLiteral("Update package signature verification failed")
                : signatureResult.errorMessage());
    }

    const QJsonArray components = manifest[QStringLiteral("components")].toArray();
    for (const QJsonValue& componentValue : components) {
        const QJsonObject componentObject = componentValue.toObject();
        const QString name = componentObject[QStringLiteral("name")].toString();
        const QString componentVersion = componentObject[QStringLiteral("version")].toString();
        const QString sha256 = componentObject[QStringLiteral("sha256")].toString();
        const QString relativePath = componentObject[QStringLiteral("file")].toString();

        auto resolvedPath = resolvePackagePath(pkgDir, relativePath);
        if (resolvedPath.isErr()) {
            m_updateRepo.updatePackageStatus(packageId, UpdatePackageStatus::Rejected);
            return Result<UpdatePackage>::err(resolvedPath.errorCode(), resolvedPath.errorMessage());
        }

        auto digestResult = m_verifier.verifyFileDigest(resolvedPath.value(), sha256);
        if (!digestResult.isOk() || !digestResult.value()) {
            m_updateRepo.updatePackageStatus(packageId, UpdatePackageStatus::Rejected);
            return Result<UpdatePackage>::err(
                digestResult.isOk() ? ErrorCode::PackageCorrupt : digestResult.errorCode(),
                digestResult.isOk()
                    ? QStringLiteral("Component digest mismatch: %1").arg(name)
                    : digestResult.errorMessage());
        }

        UpdateComponent component;
        component.packageId = packageId;
        component.name = name;
        component.version = componentVersion;
        component.sha256Hex = sha256;
        component.componentFilePath = resolvedPath.value();

        auto componentInsertResult = m_updateRepo.insertComponent(component);
        if (!componentInsertResult.isOk()) {
            m_updateRepo.updatePackageStatus(packageId, UpdatePackageStatus::Rejected);
            return Result<UpdatePackage>::err(componentInsertResult.errorCode(),
                                              componentInsertResult.errorMessage());
        }
    }

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::UpdateImported,
                               QStringLiteral("UpdatePackage"), packageId,
                               {}, {
                                   {QStringLiteral("version"), version},
                                   {QStringLiteral("status"), QStringLiteral("Staged")}
                               });

    Logger::instance().info(QStringLiteral("UpdateService"),
                            QStringLiteral("Update package staged"),
                            {
                                {QStringLiteral("package_id"), packageId},
                                {QStringLiteral("version"), version}
                            });

    return insertResult;
}

Result<void> UpdateService::applyPackage(const QString& packageId,
                                         const QString& currentVersion,
                                         const QString& actorUserId,
                                         const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return authResult;

    auto pkgResult = m_updateRepo.getPackage(packageId);
    if (!pkgResult.isOk())
        return Result<void>::err(pkgResult.errorCode(), pkgResult.errorMessage());

    const UpdatePackage& pkg = pkgResult.value();
    if (pkg.status != UpdatePackageStatus::Staged) {
        return Result<void>::err(
            ErrorCode::InvalidState,
            QStringLiteral("Only Staged packages can be applied"));
    }

    auto componentsResult = m_updateRepo.getComponents(packageId);
    if (!componentsResult.isOk())
        return Result<void>::err(componentsResult.errorCode(), componentsResult.errorMessage());
    if (componentsResult.value().isEmpty()) {
        return Result<void>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Staged update package contains no components"));
    }

    InstallHistoryEntry history;
    history.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    history.packageId = packageId;
    history.fromVersion = currentVersion;
    history.toVersion = pkg.version;
    history.appliedAt = QDateTime::currentDateTimeUtc();
    history.appliedByUserId = actorUserId;

    const QString liveDir = liveComponentsDir();
    const QString backupDir = historyBackupDir(history.id);
    if (!QDir().mkpath(liveDir) || !QDir().mkpath(backupDir)) {
        return Result<void>::err(
            ErrorCode::IoError,
            QStringLiteral("Cannot prepare update deployment directories"));
    }

    struct DeploymentRecord {
        QString name;
        QString livePath;
        QString backupPath;
        QString expectedSha256;
        bool previousExists = false;
    };

    QList<DeploymentRecord> deployed;
    deployed.reserve(componentsResult.value().size());

    auto revertDeployment = [&deployed]() {
        for (int i = deployed.size() - 1; i >= 0; --i) {
            const DeploymentRecord& rec = deployed.at(i);
            QFile::remove(rec.livePath);
            if (rec.previousExists && QFileInfo::exists(rec.backupPath)) {
                QString restoreError;
                copyFileReplacing(rec.backupPath, rec.livePath, &restoreError);
            }
        }
    };

    QJsonArray componentSnapshots;
    for (const UpdateComponent& component : componentsResult.value()) {
        if (!QFileInfo::exists(component.componentFilePath)) {
            revertDeployment();
            return Result<void>::err(
                ErrorCode::PackageCorrupt,
                QStringLiteral("Staged component is missing: %1").arg(component.name));
        }

        auto stagedDigestResult = m_verifier.verifyFileDigest(component.componentFilePath,
                                                              component.sha256Hex);
        if (!stagedDigestResult.isOk() || !stagedDigestResult.value()) {
            revertDeployment();
            return Result<void>::err(
                stagedDigestResult.isOk() ? ErrorCode::PackageCorrupt
                                          : stagedDigestResult.errorCode(),
                stagedDigestResult.isOk()
                    ? QStringLiteral("Staged component digest mismatch: %1").arg(component.name)
                    : stagedDigestResult.errorMessage());
        }

        DeploymentRecord rec;
        rec.name = component.name;
        rec.livePath = liveDir + QStringLiteral("/") + component.name;
        rec.backupPath = backupDir + QStringLiteral("/") + component.name;
        rec.expectedSha256 = component.sha256Hex;
        rec.previousExists = QFileInfo::exists(rec.livePath);

        if (rec.previousExists) {
            QString backupError;
            if (!copyFileReplacing(rec.livePath, rec.backupPath, &backupError)) {
                revertDeployment();
                return Result<void>::err(ErrorCode::IoError, backupError);
            }
        }

        QString deployError;
        if (!copyFileReplacing(component.componentFilePath, rec.livePath, &deployError)) {
            revertDeployment();
            return Result<void>::err(ErrorCode::IoError, deployError);
        }

        auto deployedDigestResult = m_verifier.verifyFileDigest(rec.livePath, rec.expectedSha256);
        if (!deployedDigestResult.isOk() || !deployedDigestResult.value()) {
            revertDeployment();
            return Result<void>::err(
                deployedDigestResult.isOk() ? ErrorCode::PackageCorrupt
                                            : deployedDigestResult.errorCode(),
                deployedDigestResult.isOk()
                    ? QStringLiteral("Post-apply digest mismatch: %1").arg(component.name)
                    : deployedDigestResult.errorMessage());
        }

        QJsonObject snapshot;
        snapshot[QStringLiteral("name")] = rec.name;
        snapshot[QStringLiteral("live_path")] = rec.livePath;
        snapshot[QStringLiteral("expected_sha256")] = rec.expectedSha256;
        snapshot[QStringLiteral("previous_exists")] = rec.previousExists;
        snapshot[QStringLiteral("previous_backup_path")] = rec.previousExists ? rec.backupPath
                                                                                : QString();
        componentSnapshots.append(snapshot);
        deployed.append(rec);
    }

    QJsonObject snapshotRoot;
    snapshotRoot[QStringLiteral("live_dir")] = liveDir;
    snapshotRoot[QStringLiteral("backup_dir")] = backupDir;
    snapshotRoot[QStringLiteral("components")] = componentSnapshots;
    history.snapshotPayloadJson = QString::fromUtf8(
        QJsonDocument(snapshotRoot).toJson(QJsonDocument::Compact));

    auto historyResult = m_updateRepo.insertInstallHistory(history);
    if (!historyResult.isOk()) {
        revertDeployment();
        return Result<void>::err(historyResult.errorCode(), historyResult.errorMessage());
    }

    auto statusResult = m_updateRepo.updatePackageStatus(packageId, UpdatePackageStatus::Applied);
    if (!statusResult.isOk()) {
        revertDeployment();
        return Result<void>::err(statusResult.errorCode(), statusResult.errorMessage());
    }

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::UpdateApplied,
                               QStringLiteral("UpdatePackage"), packageId,
                               {{QStringLiteral("version"), currentVersion}},
                               {{QStringLiteral("version"), pkg.version}});

    Logger::instance().info(QStringLiteral("UpdateService"),
                            QStringLiteral("Update package applied"),
                            {
                                {QStringLiteral("package_id"), packageId},
                                {QStringLiteral("from_version"), currentVersion},
                                {QStringLiteral("to_version"), pkg.version}
                            });

    return Result<void>::ok();
}

Result<RollbackRecord> UpdateService::rollback(const QString& installHistoryId,
                                               const QString& rationale,
                                               const QString& actorUserId,
                                               const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return Result<RollbackRecord>::err(authResult.errorCode(), authResult.errorMessage());

    if (rationale.trimmed().isEmpty()) {
        return Result<RollbackRecord>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Rollback rationale is required"));
    }

    auto historyResult = m_updateRepo.getInstallHistory(installHistoryId);
    if (!historyResult.isOk())
        return Result<RollbackRecord>::err(historyResult.errorCode(), historyResult.errorMessage());

    const InstallHistoryEntry& history = historyResult.value();

    const QJsonDocument snapshotDoc = QJsonDocument::fromJson(history.snapshotPayloadJson.toUtf8());
    if (!snapshotDoc.isObject()) {
        return Result<RollbackRecord>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Install history snapshot is invalid"));
    }

    const QJsonObject snapshotRoot = snapshotDoc.object();
    const QJsonArray componentSnapshots = snapshotRoot[QStringLiteral("components")].toArray();
    for (const QJsonValue& componentValue : componentSnapshots) {
        const QJsonObject component = componentValue.toObject();
        const QString livePath = component[QStringLiteral("live_path")].toString();
        const bool previousExists = component[QStringLiteral("previous_exists")].toBool(false);
        const QString previousBackupPath = component[QStringLiteral("previous_backup_path")].toString();

        if (livePath.isEmpty()) {
            return Result<RollbackRecord>::err(
                ErrorCode::PackageCorrupt,
                QStringLiteral("Install history snapshot has an empty live path"));
        }

        if (previousExists) {
            if (!QFileInfo::exists(previousBackupPath)) {
                return Result<RollbackRecord>::err(
                    ErrorCode::IoError,
                    QStringLiteral("Rollback backup is missing: %1").arg(previousBackupPath));
            }
            QString restoreError;
            if (!copyFileReplacing(previousBackupPath, livePath, &restoreError)) {
                return Result<RollbackRecord>::err(ErrorCode::IoError, restoreError);
            }
        } else {
            if (QFileInfo::exists(livePath) && !QFile::remove(livePath)) {
                return Result<RollbackRecord>::err(
                    ErrorCode::IoError,
                    QStringLiteral("Rollback could not remove deployed component: %1").arg(livePath));
            }
        }
    }

    QString currentVersion;
    auto latestHistoryResult = m_updateRepo.latestInstallHistory();
    if (latestHistoryResult.isOk() && latestHistoryResult.value().has_value())
        currentVersion = latestHistoryResult.value()->toVersion;

    RollbackRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.installHistoryId = installHistoryId;
    record.fromVersion = currentVersion;
    record.toVersion = history.fromVersion;
    record.rationale = rationale;
    record.rolledBackAt = QDateTime::currentDateTimeUtc();
    record.rolledBackByUserId = actorUserId;

    auto insertResult = m_updateRepo.insertRollbackRecord(record);
    if (!insertResult.isOk())
        return Result<RollbackRecord>::err(insertResult.errorCode(), insertResult.errorMessage());

    auto statusResult = m_updateRepo.updatePackageStatus(history.packageId, UpdatePackageStatus::RolledBack);
    if (!statusResult.isOk())
        return Result<RollbackRecord>::err(statusResult.errorCode(), statusResult.errorMessage());

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::UpdateRolledBack,
                               QStringLiteral("UpdatePackage"), history.packageId,
                               {{QStringLiteral("version"), currentVersion}},
                               {{QStringLiteral("version"), history.fromVersion},
                                {QStringLiteral("rationale"), rationale}});

    Logger::instance().info(QStringLiteral("UpdateService"),
                            QStringLiteral("Update rolled back"),
                            {
                                {QStringLiteral("from_version"), record.fromVersion},
                                {QStringLiteral("to_version"), record.toVersion}
                            });

    return insertResult;
}

Result<void> UpdateService::cancelPackage(const QString& packageId,
                                          const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::SecurityAdministrator);
    if (authResult.isErr())
        return authResult;

    auto pkgResult = m_updateRepo.getPackage(packageId);
    if (!pkgResult.isOk())
        return Result<void>::err(pkgResult.errorCode(), pkgResult.errorMessage());

    if (pkgResult.value().status != UpdatePackageStatus::Staged) {
        return Result<void>::err(
            ErrorCode::InvalidState,
            QStringLiteral("Only Staged packages can be cancelled"));
    }

    auto result = m_updateRepo.updatePackageStatus(packageId, UpdatePackageStatus::Cancelled);
    if (!result.isOk())
        return result;

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::UpdateImported,
                               QStringLiteral("UpdatePackage"), packageId,
                               {}, {
                                   {QStringLiteral("status"), QStringLiteral("Cancelled")}
                               });

    return Result<void>::ok();
}

Result<QList<UpdatePackage>> UpdateService::listPackages(const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::SecurityAdministrator);
    if (authResult.isErr())
        return Result<QList<UpdatePackage>>::err(authResult.errorCode(), authResult.errorMessage());

    return m_updateRepo.listPackages();
}

Result<QList<InstallHistoryEntry>> UpdateService::listInstallHistory(const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::SecurityAdministrator);
    if (authResult.isErr())
        return Result<QList<InstallHistoryEntry>>::err(authResult.errorCode(), authResult.errorMessage());

    return m_updateRepo.listInstallHistory();
}

Result<QList<RollbackRecord>> UpdateService::listRollbackRecords(const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::SecurityAdministrator);
    if (authResult.isErr())
        return Result<QList<RollbackRecord>>::err(authResult.errorCode(), authResult.errorMessage());

    return m_updateRepo.listRollbackRecords();
}

QString UpdateService::buildComponentSnapshot(const QString& packageId)
{
    auto componentsResult = m_updateRepo.getComponents(packageId);
    QJsonArray components;
    if (componentsResult.isOk()) {
        for (const UpdateComponent& component : componentsResult.value()) {
            QJsonObject componentObject;
            componentObject[QStringLiteral("name")] = component.name;
            componentObject[QStringLiteral("version")] = component.version;
            componentObject[QStringLiteral("sha256")] = component.sha256Hex;
            componentObject[QStringLiteral("component_file_path")] = component.componentFilePath;
            components.append(componentObject);
        }
    }

    return QString::fromUtf8(QJsonDocument(components).toJson(QJsonDocument::Compact));
}

Result<QString> UpdateService::resolvePackagePath(const QString& packageRoot,
                                                  const QString& relativePath) const
{
    const QString cleanedRelative = normalizedPath(relativePath);
    if (cleanedRelative.isEmpty()) {
        return Result<QString>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Package component path is empty"));
    }
    if (QFileInfo(cleanedRelative).isAbsolute()
        || cleanedRelative == QStringLiteral("..")
        || cleanedRelative.startsWith(QStringLiteral("../"))
        || cleanedRelative.contains(QStringLiteral("/../"))) {
        return Result<QString>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Package component path escapes package root"));
    }

    const QString rootCanonical = normalizedPath(QFileInfo(packageRoot).canonicalFilePath());
    const QString candidatePath = QDir(packageRoot).absoluteFilePath(cleanedRelative);
    const QString candidateCanonical = normalizedPath(QFileInfo(candidatePath).canonicalFilePath());
    if (rootCanonical.isEmpty() || candidateCanonical.isEmpty()) {
        return Result<QString>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Package component file is missing"));
    }
    if (!candidateCanonical.startsWith(rootCanonical + QStringLiteral("/"))
        && candidateCanonical != rootCanonical) {
        return Result<QString>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Package component path escapes package root"));
    }

    return Result<QString>::ok(candidateCanonical);
}
