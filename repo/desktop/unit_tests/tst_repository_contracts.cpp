// tst_repository_contracts.cpp — ProctorOps
// Direct unit coverage for repository contracts using real SQLite + migrations.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QUuid>

#include "utils/Migration.h"
#include "repositories/UserRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/MemberRepository.h"
#include "repositories/CheckInRepository.h"
#include "repositories/QuestionRepository.h"
#include "repositories/KnowledgePointRepository.h"
#include "repositories/IngestionRepository.h"
#include "repositories/SyncRepository.h"
#include "repositories/UpdateRepository.h"

namespace {

void runMigrations(QSqlDatabase& db)
{
    const QString basePath = QStringLiteral(SOURCE_ROOT "/database/migrations");
    Migration runner(db, basePath);
    const auto result = runner.applyPending();
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

void seedUser(QSqlDatabase& db,
              const QString& id,
              const QString& username,
              Role role = Role::SecurityAdministrator)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO users "
        "(id, username, role, status, created_at, updated_at, created_by_user_id) "
        "VALUES (?, ?, ?, 'Active', ?, ?, NULL)"));
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    q.addBindValue(id);
    q.addBindValue(username);
    q.addBindValue(roleToString(role));
    q.addBindValue(now);
    q.addBindValue(now);
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

TrustedSigningKey makeSigningKey(const QString& id,
                                 const QString& label,
                                 const QString& importedBy)
{
    TrustedSigningKey key;
    key.id = id;
    key.label = label;
    key.publicKeyDerHex = QStringLiteral("302a300506032b6570032100") + QString(64, QLatin1Char('a'));
    key.fingerprint = id + QStringLiteral("-fp");
    key.importedAt = QDateTime::currentDateTimeUtc();
    key.importedByUserId = importedBy;
    key.expiresAt = QDateTime();
    key.revoked = false;
    return key;
}

}

class TstRepositoryContracts : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void test_userRepository_crudAndSession();
    void test_auditRepository_chainAndRequestPersistence();
    void test_memberAndCheckinRepositories_roundTrip();
    void test_questionAndKnowledgePointRepositories_roundTrip();
    void test_ingestionRepository_jobLifecycleAndCheckpoint();
    void test_syncAndUpdateRepositories_roundTrip();

private:
    QSqlDatabase m_db;
    int m_dbIndex = 0;
};

void TstRepositoryContracts::init()
{
    const QString connName = QStringLiteral("tst_repo_contracts_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));
    QSqlQuery pragma(m_db);
    QVERIFY2(pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON;")), qPrintable(pragma.lastError().text()));

    runMigrations(m_db);

    seedUser(m_db, QStringLiteral("u-admin"), QStringLiteral("admin"), Role::SecurityAdministrator);
    seedUser(m_db, QStringLiteral("u-operator"), QStringLiteral("operator"), Role::FrontDeskOperator);
}

void TstRepositoryContracts::cleanup()
{
    const QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstRepositoryContracts::test_userRepository_crudAndSession()
{
    UserRepository repo(m_db);

    User user;
    user.id = QStringLiteral("u-content");
    user.username = QStringLiteral("content_manager");
    user.role = Role::ContentManager;
    user.status = UserStatus::Active;
    user.createdAt = QDateTime::currentDateTimeUtc();
    user.updatedAt = user.createdAt;
    user.createdByUserId = QStringLiteral("u-admin");

    auto ins = repo.insertUser(user);
    QVERIFY(ins.isOk());

    auto byName = repo.findUserByUsername(QStringLiteral("content_manager"));
    QVERIFY(byName.isOk());
    QCOMPARE(byName.value().id, QStringLiteral("u-content"));

    Credential cred;
    cred.userId = user.id;
    cred.algorithm = QStringLiteral("argon2id");
    cred.timeCost = 3;
    cred.memoryCost = 65536;
    cred.parallelism = 4;
    cred.tagLength = 32;
    cred.saltHex = QString(32, QLatin1Char('b'));
    cred.hashHex = QString(64, QLatin1Char('c'));
    cred.updatedAt = QDateTime::currentDateTimeUtc();

    QVERIFY(repo.upsertCredential(cred).isOk());
    auto gotCred = repo.getCredential(user.id);
    QVERIFY(gotCred.isOk());
    QCOMPARE(gotCred.value().algorithm, QStringLiteral("argon2id"));

    UserSession session;
    session.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.userId = user.id;
    session.createdAt = QDateTime::currentDateTimeUtc();
    session.lastActiveAt = session.createdAt;
    session.active = true;
    QVERIFY(repo.insertSession(session).isOk());

    auto loadedSession = repo.findSession(session.token);
    QVERIFY(loadedSession.isOk());
    QVERIFY(loadedSession.value().active);

    QVERIFY(repo.deactivateSession(session.token).isOk());
    loadedSession = repo.findSession(session.token);
    QVERIFY(loadedSession.isOk());
    QVERIFY(!loadedSession.value().active);
}

void TstRepositoryContracts::test_auditRepository_chainAndRequestPersistence()
{
    AuditRepository repo(m_db);

    AuditEntry entry;
    entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.timestamp = QDateTime::currentDateTimeUtc();
    entry.actorUserId = QStringLiteral("u-admin");
    entry.eventType = AuditEventType::Login;
    entry.entityType = QStringLiteral("User");
    entry.entityId = QStringLiteral("u-admin");
    entry.beforePayloadJson = QStringLiteral("{}");
    entry.afterPayloadJson = QStringLiteral("{}");
    entry.previousEntryHash = QStringLiteral("");
    entry.entryHash = QString(64, QLatin1Char('a'));

    auto ins = repo.insertEntry(entry);
    QVERIFY2(ins.isOk(), ins.isErr() ? qPrintable(ins.errorMessage()) : "");

    auto head = repo.getChainHeadHash();
    QVERIFY(head.isOk());
    QCOMPARE(head.value(), QString(64, QLatin1Char('a')));

    // Export/deletion requests reference members(id), so seed one concrete member.
    QSqlQuery seedMember(m_db);
    seedMember.prepare(QStringLiteral(
        "INSERT INTO members (id, member_id, member_id_hash, barcode_encrypted, mobile_encrypted, name_encrypted, deleted, created_at, updated_at) "
        "VALUES ('member-1', 'enc-member-1', 'hash-member-1', 'enc-barcode', 'enc-mobile', 'enc-name', 0, ?, ?)"));
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    seedMember.addBindValue(now);
    seedMember.addBindValue(now);
    QVERIFY2(seedMember.exec(), qPrintable(seedMember.lastError().text()));

    ExportRequest ex;
    ex.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ex.memberId = QStringLiteral("member-1");
    ex.requesterUserId = QStringLiteral("u-admin");
    ex.status = QStringLiteral("PENDING");
    ex.rationale = QStringLiteral("compliance export");
    ex.createdAt = QDateTime::currentDateTimeUtc();
    auto exIns = repo.insertExportRequest(ex);
    QVERIFY(exIns.isOk());

    auto exGet = repo.getExportRequest(ex.id);
    QVERIFY(exGet.isOk());
    QCOMPARE(exGet.value().status, QStringLiteral("PENDING"));

    DeletionRequest del;
    del.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    del.memberId = QStringLiteral("member-1");
    del.requesterUserId = QStringLiteral("u-admin");
    del.approverUserId = QString();
    del.status = QStringLiteral("PENDING");
    del.rationale = QStringLiteral("gdpr erase");
    del.createdAt = QDateTime::currentDateTimeUtc();
    auto delIns = repo.insertDeletionRequest(del);
    QVERIFY(delIns.isOk());

    auto delGet = repo.getDeletionRequest(del.id);
    QVERIFY(delGet.isOk());
    QCOMPARE(delGet.value().status, QStringLiteral("PENDING"));
}

void TstRepositoryContracts::test_memberAndCheckinRepositories_roundTrip()
{
    MemberRepository memberRepo(m_db);
    memberRepo.setDecryptor([](const QString&, const QString& value) { return value; });

    Member member;
    member.id = QStringLiteral("m-1");
    member.memberId = QStringLiteral("MID-001");
    member.barcodeEncrypted = QStringLiteral("BC-001");
    member.mobileEncrypted = QStringLiteral("(555) 000-1111");
    member.nameEncrypted = QStringLiteral("Test Member");
    member.deleted = false;
    member.createdAt = QDateTime::currentDateTimeUtc();
    member.updatedAt = member.createdAt;
    QVERIFY(memberRepo.insertMember(member).isOk());

    auto byHumanId = memberRepo.findMemberByMemberId(QStringLiteral("MID-001"));
    QVERIFY(byHumanId.isOk());
    QCOMPARE(byHumanId.value().id, QStringLiteral("m-1"));

    TermCard term;
    term.id = QStringLiteral("t-1");
    term.memberId = member.id;
    term.termStart = QDate::currentDate().addDays(-1);
    term.termEnd = QDate::currentDate().addDays(30);
    term.active = true;
    term.createdAt = QDateTime::currentDateTimeUtc();
    QVERIFY(memberRepo.insertTermCard(term).isOk());

    PunchCard punch;
    punch.id = QStringLiteral("p-1");
    punch.memberId = member.id;
    punch.productCode = QStringLiteral("STANDARD");
    punch.initialBalance = 5;
    punch.currentBalance = 5;
    punch.createdAt = QDateTime::currentDateTimeUtc();
    punch.updatedAt = punch.createdAt;
    QVERIFY(memberRepo.insertPunchCard(punch).isOk());

    CheckInRepository checkinRepo(m_db);

    CheckInAttempt attempt;
    attempt.id = QStringLiteral("a-1");
    attempt.memberId = member.id;
    attempt.sessionId = QStringLiteral("S-01");
    attempt.operatorUserId = QStringLiteral("u-operator");
    attempt.status = CheckInStatus::Success;
    attempt.attemptedAt = QDateTime::currentDateTimeUtc();
    attempt.deductionEventId = QString();
    attempt.failureReason = QString();
    auto atIns = checkinRepo.insertAttempt(attempt);
    QVERIFY(atIns.isOk());

    auto recent = checkinRepo.findRecentSuccess(member.id, QStringLiteral("S-01"),
                                                QDateTime::currentDateTimeUtc().addSecs(-30));
    QVERIFY(recent.isOk());
    QVERIFY(recent.value().has_value());
    QCOMPARE(recent.value()->id, QStringLiteral("a-1"));
}

void TstRepositoryContracts::test_questionAndKnowledgePointRepositories_roundTrip()
{
    KnowledgePointRepository kpRepo(m_db);
    QuestionRepository qRepo(m_db);

    KnowledgePoint root;
    root.id = QStringLiteral("kp-root");
    root.name = QStringLiteral("Root");
    root.parentId = QString();
    root.position = 1;
    root.path = QString();
    root.createdAt = QDateTime::currentDateTimeUtc();
    root.deleted = false;

    auto kpIns = kpRepo.insertKP(root);
    QVERIFY(kpIns.isOk());
    QCOMPARE(kpIns.value().path, QStringLiteral("Root"));

    Tag tag;
    tag.id = QStringLiteral("tag-1");
    tag.name = QStringLiteral("safety");
    tag.createdAt = QDateTime::currentDateTimeUtc();
    QVERIFY(qRepo.insertTag(tag).isOk());

    Question q;
    q.id = QStringLiteral("q-1");
    q.bodyText = QStringLiteral("What is 2+2?");
    q.answerOptions = {QStringLiteral("3"), QStringLiteral("4")};
    q.correctAnswerIndex = 1;
    q.difficulty = 2;
    q.discrimination = 0.55;
    q.status = QuestionStatus::Active;
    q.externalId = QStringLiteral("EXT-001");
    q.createdAt = QDateTime::currentDateTimeUtc();
    q.updatedAt = q.createdAt;
    q.createdByUserId = QStringLiteral("u-admin");
    q.updatedByUserId = QStringLiteral("u-admin");

    QVERIFY(qRepo.insertQuestion(q).isOk());

    QuestionKPMapping map;
    map.questionId = q.id;
    map.knowledgePointId = root.id;
    map.mappedAt = QDateTime::currentDateTimeUtc();
    map.mappedByUserId = QStringLiteral("u-admin");
    QVERIFY(qRepo.insertKPMapping(map).isOk());

    auto extExists = qRepo.externalIdExists(QStringLiteral("EXT-001"));
    QVERIFY(extExists.isOk());
    QVERIFY(extExists.value());

    QuestionFilter filter;
    filter.statusFilter = QuestionStatus::Active;
    auto list = qRepo.queryQuestions(filter);
    QVERIFY(list.isOk());
    QVERIFY(!list.value().isEmpty());
}

void TstRepositoryContracts::test_ingestionRepository_jobLifecycleAndCheckpoint()
{
    IngestionRepository repo(m_db);

    IngestionJob dep;
    dep.id = QStringLiteral("job-dep");
    dep.type = JobType::QuestionImport;
    dep.status = JobStatus::Pending;
    dep.priority = 5;
    dep.sourceFilePath = QStringLiteral("/tmp/dep.jsonl");
    dep.scheduledAt = QDateTime();
    dep.createdAt = QDateTime::currentDateTimeUtc();
    dep.startedAt = QDateTime();
    dep.completedAt = QDateTime();
    dep.failedAt = QDateTime();
    dep.retryCount = 0;
    dep.lastError = QString();
    dep.currentPhase = JobPhase::Validate;
    dep.createdByUserId = QStringLiteral("u-admin");
    QVERIFY(repo.insertJob(dep).isOk());

    IngestionJob job = dep;
    job.id = QStringLiteral("job-main");
    job.sourceFilePath = QStringLiteral("/tmp/main.jsonl");
    QVERIFY(repo.insertJob(job).isOk());

    JobDependency link;
    link.jobId = job.id;
    link.dependsOnJobId = dep.id;
    QVERIFY(repo.insertDependency(link).isOk());

    auto depsMetBefore = repo.areDependenciesMet(job.id);
    QVERIFY(depsMetBefore.isOk());
    QVERIFY(!depsMetBefore.value());

    QVERIFY(repo.updateJobStatus(dep.id, JobStatus::Completed).isOk());
    auto depsMetAfter = repo.areDependenciesMet(job.id);
    QVERIFY(depsMetAfter.isOk());
    QVERIFY(depsMetAfter.value());

    JobCheckpoint cp;
    cp.jobId = job.id;
    cp.phase = JobPhase::Import;
    cp.offsetBytes = 128;
    cp.recordsProcessed = 10;
    cp.savedAt = QDateTime::currentDateTimeUtc();
    QVERIFY(repo.saveCheckpoint(cp).isOk());

    auto loaded = repo.loadCheckpoint(job.id, JobPhase::Import);
    QVERIFY(loaded.isOk());
    QVERIFY(loaded.value().has_value());
    QCOMPARE(loaded.value()->offsetBytes, 128);
}

void TstRepositoryContracts::test_syncAndUpdateRepositories_roundTrip()
{
    SyncRepository syncRepo(m_db);
    UpdateRepository updateRepo(m_db);

    auto key = makeSigningKey(QStringLiteral("k-1"), QStringLiteral("unit key"), QStringLiteral("u-admin"));
    QVERIFY(syncRepo.insertSigningKey(key).isOk());

    SyncPackage pkg;
    pkg.id = QStringLiteral("pkg-1");
    pkg.sourceDeskId = QStringLiteral("desk-A");
    pkg.signerKeyId = key.id;
    pkg.exportedAt = QDateTime::currentDateTimeUtc();
    pkg.sinceWatermark = pkg.exportedAt.addSecs(-600);
    pkg.status = SyncPackageStatus::Pending;
    pkg.packageFilePath = QStringLiteral("C:/tmp/pkg-1.proctorsync");
    pkg.importedAt = QDateTime();
    pkg.importedByUserId = QString();
    QVERIFY(syncRepo.insertPackage(pkg).isOk());

    SyncPackageEntity entity;
    entity.packageId = pkg.id;
    entity.entityType = QStringLiteral("deductions");
    entity.filePath = QStringLiteral("deductions.jsonl");
    entity.sha256Hex = QString(64, QLatin1Char('d'));
    entity.recordCount = 1;
    entity.verified = false;
    entity.applied = false;
    entity.appliedAt = QDateTime();
    QVERIFY(syncRepo.insertPackageEntity(entity).isOk());

    QVERIFY(syncRepo.markEntityVerified(pkg.id, entity.entityType).isOk());
    auto entities = syncRepo.getPackageEntities(pkg.id);
    QVERIFY(entities.isOk());
    QVERIFY(!entities.value().isEmpty());
    QVERIFY(entities.value().first().verified);

    UpdatePackage up;
    up.id = QStringLiteral("upd-1");
    up.version = QStringLiteral("1.1.0");
    up.targetPlatform = QStringLiteral("windows-x86_64");
    up.description = QStringLiteral("unit update");
    up.signerKeyId = key.id;
    up.signatureValid = true;
    up.stagedPath = QStringLiteral("C:/tmp/upd-1");
    up.status = UpdatePackageStatus::Staged;
    up.importedAt = QDateTime::currentDateTimeUtc();
    up.importedByUserId = QStringLiteral("u-admin");
    QVERIFY(updateRepo.insertPackage(up).isOk());

    UpdateComponent c;
    c.packageId = up.id;
    c.name = QStringLiteral("proctorops.exe");
    c.version = up.version;
    c.sha256Hex = QString(64, QLatin1Char('e'));
    c.componentFilePath = QStringLiteral("proctorops.exe");
    QVERIFY(updateRepo.insertComponent(c).isOk());

    auto comps = updateRepo.getComponents(up.id);
    QVERIFY(comps.isOk());
    QCOMPARE(comps.value().size(), 1);

    InstallHistoryEntry hist;
    hist.id = QStringLiteral("hist-1");
    hist.packageId = up.id;
    hist.fromVersion = QStringLiteral("1.0.0");
    hist.toVersion = QStringLiteral("1.1.0");
    hist.appliedAt = QDateTime::currentDateTimeUtc();
    hist.appliedByUserId = QStringLiteral("u-admin");
    hist.snapshotPayloadJson = QStringLiteral("{}");
    QVERIFY(updateRepo.insertInstallHistory(hist).isOk());

    RollbackRecord rb;
    rb.id = QStringLiteral("rb-1");
    rb.installHistoryId = hist.id;
    rb.fromVersion = QStringLiteral("1.1.0");
    rb.toVersion = QStringLiteral("1.0.0");
    rb.rationale = QStringLiteral("verification rollback");
    rb.rolledBackAt = QDateTime::currentDateTimeUtc();
    rb.rolledBackByUserId = QStringLiteral("u-admin");
    QVERIFY(updateRepo.insertRollbackRecord(rb).isOk());

    auto rollbacks = updateRepo.listRollbackRecords();
    QVERIFY(rollbacks.isOk());
    QVERIFY(!rollbacks.value().isEmpty());
}

QTEST_MAIN(TstRepositoryContracts)
#include "tst_repository_contracts.moc"
