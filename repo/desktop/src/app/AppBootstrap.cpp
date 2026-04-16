#include "app/AppBootstrap.h"

#include "app/AppContext.h"
#include "app/AppSettings.h"

#include "crypto/KeyStore.h"
#include "crypto/AesGcmCipher.h"

#include "repositories/UserRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/QuestionRepository.h"
#include "repositories/KnowledgePointRepository.h"
#include "repositories/MemberRepository.h"
#include "repositories/CheckInRepository.h"
#include "repositories/IngestionRepository.h"
#include "repositories/SyncRepository.h"
#include "repositories/UpdateRepository.h"

#include "services/AuditService.h"
#include "services/AuthService.h"
#include "services/QuestionService.h"
#include "services/CheckInService.h"
#include "services/IngestionService.h"
#include "services/SyncService.h"
#include "services/DataSubjectService.h"
#include "services/UpdateService.h"
#include "services/PackageVerifier.h"

#include "scheduler/JobScheduler.h"

#include "utils/Logger.h"

#include <QDir>
#include <QStandardPaths>
#include <QtSql/QSqlDatabase>

std::unique_ptr<AppContext> buildAppContext(QSqlDatabase& db,
                                            const AppSettings& /*settings*/)
{
    auto ctx = std::make_unique<AppContext>();

    const QString keyDir = QStandardPaths::writableLocation(
                               QStandardPaths::AppLocalDataLocation)
                           + QStringLiteral("/keys");
    QDir().mkpath(keyDir);
    ctx->keyStore = std::make_unique<KeyStore>(keyDir);

    auto masterKeyResult = ctx->keyStore->getMasterKey();
    if (!masterKeyResult.isOk()) {
        Logger::instance().error(
            QStringLiteral("startup"),
            QStringLiteral("Fatal: cannot read master key"),
            {{QStringLiteral("error"), masterKeyResult.errorMessage()}});
        return nullptr;
    }
    ctx->cipher = std::make_unique<AesGcmCipher>(masterKeyResult.value());

    ctx->userRepo      = std::make_unique<UserRepository>(db);
    ctx->auditRepo     = std::make_unique<AuditRepository>(db);
    ctx->questionRepo  = std::make_unique<QuestionRepository>(db);
    ctx->kpRepo        = std::make_unique<KnowledgePointRepository>(db);
    ctx->memberRepo    = std::make_unique<MemberRepository>(db);
    ctx->checkInRepo   = std::make_unique<CheckInRepository>(db);
    ctx->ingestionRepo = std::make_unique<IngestionRepository>(db);
    ctx->memberRepo->setEncryptor([cipher = ctx->cipher.get()](const QString& fieldName,
                                                                const QString& plaintext) {
        QByteArray context;
        if (fieldName == QStringLiteral("member_id"))
            context = QByteArrayLiteral("member.member_id");

        auto encryptResult = cipher->encrypt(plaintext, context);
        return encryptResult.isOk() ? QString::fromLatin1(encryptResult.value().toBase64()) : QString();
    });
    ctx->memberRepo->setDecryptor([cipher = ctx->cipher.get()](const QString& fieldName,
                                                                const QString& ciphertextBase64) {
        QByteArray context;
        if (fieldName == QStringLiteral("barcode"))
            context = QByteArrayLiteral("member.barcode");
        else if (fieldName == QStringLiteral("mobile"))
            context = QByteArrayLiteral("member.mobile");
        else if (fieldName == QStringLiteral("name"))
            context = QByteArrayLiteral("member.name");
        else if (fieldName == QStringLiteral("member_id"))
            context = QByteArrayLiteral("member.member_id");

        auto decryptResult = cipher->decrypt(QByteArray::fromBase64(ciphertextBase64.toLatin1()),
                                             context);
        return decryptResult.isOk() ? decryptResult.value() : QString();
    });

    ctx->auditService    = std::make_unique<AuditService>(*ctx->auditRepo, *ctx->cipher);
    ctx->authService     = std::make_unique<AuthService>(*ctx->userRepo, *ctx->auditRepo);
    ctx->auditService->setAuthService(ctx->authService.get());
    ctx->questionService = std::make_unique<QuestionService>(
                               *ctx->questionRepo, *ctx->kpRepo,
                               *ctx->auditService, *ctx->authService);
    ctx->checkInService  = std::make_unique<CheckInService>(
                               *ctx->memberRepo, *ctx->checkInRepo,
                               *ctx->authService, *ctx->auditService, *ctx->cipher, db);
    ctx->ingestionService = std::make_unique<IngestionService>(
                               *ctx->ingestionRepo, *ctx->questionRepo, *ctx->kpRepo,
                               *ctx->memberRepo, *ctx->auditService,
                               *ctx->authService, *ctx->cipher);

    ctx->syncRepo    = std::make_unique<SyncRepository>(db);
    ctx->updateRepo  = std::make_unique<UpdateRepository>(db);

    ctx->packageVerifier = std::make_unique<PackageVerifier>(*ctx->syncRepo);
    ctx->syncService = std::make_unique<SyncService>(
                           *ctx->syncRepo, *ctx->checkInRepo, *ctx->auditRepo,
                           *ctx->authService, *ctx->auditService, *ctx->packageVerifier);
    ctx->dataSubjectService = std::make_unique<DataSubjectService>(
                                  *ctx->auditRepo, *ctx->memberRepo,
                                  *ctx->authService, *ctx->auditService, *ctx->cipher);
    ctx->updateService = std::make_unique<UpdateService>(
                             *ctx->updateRepo, *ctx->syncRepo,
                             *ctx->authService, *ctx->packageVerifier, *ctx->auditService);

    ctx->jobScheduler = std::make_unique<JobScheduler>(
                            *ctx->ingestionRepo, *ctx->ingestionService, *ctx->auditService);

    return ctx;
}
