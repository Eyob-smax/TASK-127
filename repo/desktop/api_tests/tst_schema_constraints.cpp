// tst_schema_constraints.cpp — ProctorOps
// Integration-style tests that apply the full SQLite schema to a real in-memory
// database and verify uniqueness constraints, FK enforcement, CHECK constraints,
// and duplicate-window query behavior.
//
// These tests use real SQLite; no mocks are used for the database layer.
// Migration SQL is applied inline to match what the Migration runner will do.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QUuid>

class TstSchemaConstraints : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();  // called before each test — rebuilds the DB

    // Uniqueness constraints
    void test_usernameUnique();
    void test_tagNameUnique();
    void test_memberIdUnique();

    // CHECK constraints
    void test_difficultyCheckConstraint_valid();
    void test_difficultyCheckConstraint_invalid_below();
    void test_difficultyCheckConstraint_invalid_above();
    void test_discriminationCheckConstraint_valid();
    void test_discriminationCheckConstraint_invalid();
    void test_termCardDatesCheck();
    void test_punchCardBalanceCheck_notNegative();
    void test_deductionBalanceCheck();

    // Foreign key enforcement
    void test_fkEnforced_credentialRequiresUser();
    void test_fkEnforced_termCardRequiresMember();

    // Duplicate check-in window query
    void test_duplicateWindowQuery_detectsWithin30s();
    void test_duplicateWindowQuery_allowsAfter30s();

    // Audit chain head — single-row enforcement
    void test_auditChainHead_singleRow();

private:
    QSqlDatabase m_db;
    QString m_dbName;

    bool exec(const QString& sql, const QVariantList& bindings = {}) {
        QSqlQuery q(m_db);
        if (!bindings.isEmpty()) {
            q.prepare(sql);
            for (const auto& v : bindings) q.addBindValue(v);
            return q.exec();
        }
        return q.exec(sql);
    }

    QString newUuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
    QString nowUtc()  { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }

    void applyBaseSchema();
};

void TstSchemaConstraints::initTestCase()
{
    m_dbName = QStringLiteral("test_schema_constraints");
}

void TstSchemaConstraints::cleanupTestCase()
{
    m_db.close();
    QSqlDatabase::removeDatabase(m_dbName);
}

void TstSchemaConstraints::init()
{
    if (m_db.isOpen()) {
        m_db.close();
        QSqlDatabase::removeDatabase(m_dbName);
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_dbName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));
    QVERIFY(exec(QStringLiteral("PRAGMA foreign_keys = ON;")));
    applyBaseSchema();
}

void TstSchemaConstraints::applyBaseSchema()
{
    // Minimal schema sufficient for all constraint tests.
    // Mirrors the real migration SQL — kept in sync with migration files.

    exec(QStringLiteral("CREATE TABLE users ("
        "id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE,"
        "role TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Active',"
        "created_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
        "created_by_user_id TEXT"
        ");"));

    exec(QStringLiteral("CREATE TABLE credentials ("
        "user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,"
        "algorithm TEXT NOT NULL DEFAULT 'argon2id',"
        "time_cost INTEGER NOT NULL, memory_cost INTEGER NOT NULL,"
        "parallelism INTEGER NOT NULL, tag_length INTEGER NOT NULL,"
        "salt_hex TEXT NOT NULL, hash_hex TEXT NOT NULL, updated_at TEXT NOT NULL"
        ");"));

    exec(QStringLiteral("CREATE TABLE members ("
        "id TEXT PRIMARY KEY, member_id TEXT NOT NULL,"
        "member_id_hash TEXT,"
        "barcode_encrypted TEXT NOT NULL, mobile_encrypted TEXT NOT NULL,"
        "name_encrypted TEXT NOT NULL, deleted INTEGER NOT NULL DEFAULT 0,"
        "created_at TEXT NOT NULL, updated_at TEXT NOT NULL"
        ");"));
    exec(QStringLiteral("CREATE INDEX idx_members_member_id_hash ON members (member_id_hash);"));
    exec(QStringLiteral("CREATE UNIQUE INDEX idx_members_member_id_hash_unique "
                        "ON members (member_id_hash) "
                        "WHERE member_id_hash IS NOT NULL AND member_id_hash != '';"));

    exec(QStringLiteral("CREATE TABLE term_cards ("
        "id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id) ON DELETE CASCADE,"
        "term_start TEXT NOT NULL, term_end TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1,"
        "created_at TEXT NOT NULL,"
        "CONSTRAINT chk_term_dates CHECK (term_end > term_start)"
        ");"));

    exec(QStringLiteral("CREATE TABLE punch_cards ("
        "id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id) ON DELETE CASCADE,"
        "product_code TEXT NOT NULL,"
        "initial_balance INTEGER NOT NULL CHECK (initial_balance >= 0),"
        "current_balance INTEGER NOT NULL CHECK (current_balance >= 0),"
        "created_at TEXT NOT NULL, updated_at TEXT NOT NULL"
        ");"));

    exec(QStringLiteral("CREATE TABLE checkin_attempts ("
        "id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id),"
        "session_id TEXT NOT NULL, operator_user_id TEXT NOT NULL REFERENCES users(id),"
        "status TEXT NOT NULL, attempted_at TEXT NOT NULL,"
        "deduction_event_id TEXT, failure_reason TEXT"
        ");"));
    exec(QStringLiteral("CREATE INDEX idx_checkin_dedup ON checkin_attempts"
                         "(member_id, session_id, attempted_at, status);"));

    exec(QStringLiteral("CREATE TABLE deduction_events ("
        "id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id),"
        "punch_card_id TEXT NOT NULL REFERENCES punch_cards(id),"
        "checkin_attempt_id TEXT NOT NULL REFERENCES checkin_attempts(id),"
        "sessions_deducted INTEGER NOT NULL DEFAULT 1 CHECK (sessions_deducted > 0),"
        "balance_before INTEGER NOT NULL CHECK (balance_before >= 0),"
        "balance_after INTEGER NOT NULL CHECK (balance_after >= 0),"
        "deducted_at TEXT NOT NULL, reversed_by_correction_id TEXT,"
        "CONSTRAINT chk_balance CHECK (balance_after = balance_before - sessions_deducted)"
        ");"));

    exec(QStringLiteral("CREATE TABLE tags ("
        "id TEXT PRIMARY KEY, name TEXT NOT NULL, created_at TEXT NOT NULL,"
        "CONSTRAINT uq_tag_name UNIQUE (name)"
        ");"));

    exec(QStringLiteral("CREATE TABLE questions ("
        "id TEXT PRIMARY KEY, body_text TEXT NOT NULL,"
        "answer_options_json TEXT NOT NULL,"
        "correct_answer_index INTEGER NOT NULL CHECK (correct_answer_index >= 0),"
        "difficulty INTEGER NOT NULL CHECK (difficulty >= 1 AND difficulty <= 5),"
        "discrimination REAL NOT NULL CHECK (discrimination >= 0.00 AND discrimination <= 1.00),"
        "status TEXT NOT NULL DEFAULT 'Active', external_id TEXT,"
        "created_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
        "created_by_user_id TEXT NOT NULL REFERENCES users(id),"
        "updated_by_user_id TEXT NOT NULL REFERENCES users(id)"
        ");"));

    exec(QStringLiteral("CREATE TABLE audit_chain_head ("
        "id INTEGER PRIMARY KEY CHECK (id = 1),"
        "last_entry_id TEXT, last_entry_hash TEXT NOT NULL DEFAULT ''"
        ");"));
    exec(QStringLiteral("INSERT INTO audit_chain_head (id, last_entry_id, last_entry_hash)"
                         " VALUES (1, NULL, '');"));

    // Seed one user for FK tests
    exec(QStringLiteral("INSERT INTO users VALUES ('u-seed','seed','PROCTOR','Active',?,?,NULL);"),
         {nowUtc(), nowUtc()});
}

// ── Uniqueness constraints ───────────────────────────────────────────────────

void TstSchemaConstraints::test_usernameUnique()
{
    QVERIFY(exec(QStringLiteral("INSERT INTO users VALUES ('u-1','alice','PROCTOR','Active',?,?,NULL);"),
                 {nowUtc(), nowUtc()}));
    // Inserting same username must fail
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO users VALUES ('u-2','alice','PROCTOR','Active',?,?,NULL);"));
    q.addBindValue(nowUtc()); q.addBindValue(nowUtc());
    QVERIFY2(!q.exec(), "Duplicate username should have been rejected");
}

void TstSchemaConstraints::test_tagNameUnique()
{
    QVERIFY(exec(QStringLiteral("INSERT INTO tags VALUES ('t-1','Safety',?);"), {nowUtc()}));
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO tags VALUES ('t-2','Safety',?);"));
    q.addBindValue(nowUtc());
    QVERIFY2(!q.exec(), "Duplicate tag name should have been rejected");
}

void TstSchemaConstraints::test_memberIdUnique()
{
    QVERIFY(exec(QStringLiteral(
                    "INSERT INTO members "
                    "(id, member_id, member_id_hash, barcode_encrypted, mobile_encrypted, "
                    " name_encrypted, deleted, created_at, updated_at) "
                    "VALUES ('m-1','cipher-a','hash-mid-001','enc','enc','enc',0,?,?);"),
                 {nowUtc(), nowUtc()}));
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO members "
        "(id, member_id, member_id_hash, barcode_encrypted, mobile_encrypted, "
        " name_encrypted, deleted, created_at, updated_at) "
        "VALUES ('m-2','cipher-b','hash-mid-001','enc','enc','enc',0,?,?);"));
    q.addBindValue(nowUtc()); q.addBindValue(nowUtc());
    QVERIFY2(!q.exec(), "Duplicate member_id_hash should have been rejected");
}

// ── CHECK constraints ────────────────────────────────────────────────────────

void TstSchemaConstraints::test_difficultyCheckConstraint_valid()
{
    for (int d = 1; d <= 5; ++d) {
        const QString qid = newUuid();
        QVERIFY2(exec(QStringLiteral("INSERT INTO questions VALUES (?,?,?,?,?,?,?,NULL,?,?,?,?);"),
                      {qid, QStringLiteral("body"), QStringLiteral("[]"),
                       0, d, 0.5,
                       QStringLiteral("Active"), nowUtc(), nowUtc(),
                       QStringLiteral("u-seed"), QStringLiteral("u-seed")}),
                 qPrintable(QStringLiteral("Difficulty %1 should be valid").arg(d)));
    }
}

void TstSchemaConstraints::test_difficultyCheckConstraint_invalid_below()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO questions VALUES (?,?,?,?,?,?,?,NULL,?,?,?,?);"));
    q.addBindValue(newUuid()); q.addBindValue(QStringLiteral("body"));
    q.addBindValue(QStringLiteral("[]")); q.addBindValue(0);
    q.addBindValue(0);  // difficulty = 0 — invalid
    q.addBindValue(0.5); q.addBindValue(QStringLiteral("Active"));
    q.addBindValue(nowUtc()); q.addBindValue(nowUtc());
    q.addBindValue(QStringLiteral("u-seed")); q.addBindValue(QStringLiteral("u-seed"));
    QVERIFY2(!q.exec(), "Difficulty 0 should violate CHECK constraint");
}

void TstSchemaConstraints::test_difficultyCheckConstraint_invalid_above()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO questions VALUES (?,?,?,?,?,?,?,NULL,?,?,?,?);"));
    q.addBindValue(newUuid()); q.addBindValue(QStringLiteral("body"));
    q.addBindValue(QStringLiteral("[]")); q.addBindValue(0);
    q.addBindValue(6);  // difficulty = 6 — invalid
    q.addBindValue(0.5); q.addBindValue(QStringLiteral("Active"));
    q.addBindValue(nowUtc()); q.addBindValue(nowUtc());
    q.addBindValue(QStringLiteral("u-seed")); q.addBindValue(QStringLiteral("u-seed"));
    QVERIFY2(!q.exec(), "Difficulty 6 should violate CHECK constraint");
}

void TstSchemaConstraints::test_discriminationCheckConstraint_valid()
{
    for (double d : {0.00, 0.5, 1.00}) {
        const QString qid = newUuid();
        QVERIFY2(exec(QStringLiteral("INSERT INTO questions VALUES (?,?,?,?,?,?,?,NULL,?,?,?,?);"),
                      {qid, QStringLiteral("body"), QStringLiteral("[]"),
                       0, 3, d,
                       QStringLiteral("Active"), nowUtc(), nowUtc(),
                       QStringLiteral("u-seed"), QStringLiteral("u-seed")}),
                 qPrintable(QStringLiteral("Discrimination %1 should be valid").arg(d)));
    }
}

void TstSchemaConstraints::test_discriminationCheckConstraint_invalid()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO questions VALUES (?,?,?,?,?,?,?,NULL,?,?,?,?);"));
    q.addBindValue(newUuid()); q.addBindValue(QStringLiteral("body"));
    q.addBindValue(QStringLiteral("[]")); q.addBindValue(0);
    q.addBindValue(3); q.addBindValue(1.01);  // discrimination > 1.00
    q.addBindValue(QStringLiteral("Active"));
    q.addBindValue(nowUtc()); q.addBindValue(nowUtc());
    q.addBindValue(QStringLiteral("u-seed")); q.addBindValue(QStringLiteral("u-seed"));
    QVERIFY2(!q.exec(), "Discrimination 1.01 should violate CHECK constraint");
}

void TstSchemaConstraints::test_termCardDatesCheck()
{
    // Insert a member first
    exec(QStringLiteral("INSERT INTO members (id, member_id, member_id_hash, barcode_encrypted, "
                         "mobile_encrypted, name_encrypted, deleted, created_at, updated_at) "
                         "VALUES ('m-tc','MID-TC','h-mid-tc','e','e','e',0,?,?);"),
         {nowUtc(), nowUtc()});
    // Valid: term_end > term_start
    QVERIFY(exec(QStringLiteral("INSERT INTO term_cards VALUES (?,?,?,?,1,?);"),
                 {newUuid(), QStringLiteral("m-tc"),
                  QStringLiteral("2025-01-01"), QStringLiteral("2026-01-01"), nowUtc()}));
    // Invalid: term_end == term_start
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO term_cards VALUES (?,?,?,?,1,?);"));
    q.addBindValue(newUuid()); q.addBindValue(QStringLiteral("m-tc"));
    q.addBindValue(QStringLiteral("2025-06-01")); q.addBindValue(QStringLiteral("2025-06-01"));
    q.addBindValue(nowUtc());
    QVERIFY2(!q.exec(), "term_end == term_start should violate CHECK");
}

void TstSchemaConstraints::test_punchCardBalanceCheck_notNegative()
{
    exec(QStringLiteral("INSERT INTO members (id, member_id, member_id_hash, barcode_encrypted, "
                         "mobile_encrypted, name_encrypted, deleted, created_at, updated_at) "
                         "VALUES ('m-pc','MID-PC','h-mid-pc','e','e','e',0,?,?);"),
         {nowUtc(), nowUtc()});
    // Negative current_balance should fail
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO punch_cards VALUES (?,?,'P1',10,-1,?,?);"));
    q.addBindValue(newUuid()); q.addBindValue(QStringLiteral("m-pc"));
    q.addBindValue(nowUtc()); q.addBindValue(nowUtc());
    QVERIFY2(!q.exec(), "Negative current_balance should violate CHECK");
}

void TstSchemaConstraints::test_deductionBalanceCheck()
{
    // Insert prerequisites
    exec(QStringLiteral("INSERT INTO members (id, member_id, member_id_hash, barcode_encrypted, "
                         "mobile_encrypted, name_encrypted, deleted, created_at, updated_at) "
                         "VALUES ('m-ded','MID-DED','h-mid-ded','e','e','e',0,?,?);"),
         {nowUtc(), nowUtc()});
    exec(QStringLiteral("INSERT INTO punch_cards VALUES ('pc-1','m-ded','P1',10,9,?,?);"),
         {nowUtc(), nowUtc()});
    exec(QStringLiteral("INSERT INTO checkin_attempts VALUES ('ca-1','m-ded','S1','u-seed','Success',?,NULL,NULL);"),
         {nowUtc()});
    // Valid deduction: balance_after = balance_before - sessions_deducted = 10 - 1 = 9
    QVERIFY(exec(QStringLiteral("INSERT INTO deduction_events VALUES (?,?,?,?,1,10,9,?,NULL);"),
                 {newUuid(), QStringLiteral("m-ded"), QStringLiteral("pc-1"),
                  QStringLiteral("ca-1"), nowUtc()}));
    // Invalid: balance_after != balance_before - sessions_deducted (10 - 1 != 8)
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO deduction_events VALUES (?,?,?,?,1,10,8,?,NULL);"));
    q.addBindValue(newUuid()); q.addBindValue(QStringLiteral("m-ded"));
    q.addBindValue(QStringLiteral("pc-1")); q.addBindValue(QStringLiteral("ca-1"));
    q.addBindValue(nowUtc());
    QVERIFY2(!q.exec(), "Wrong balance_after should violate CHECK constraint");
}

// ── Foreign key enforcement ──────────────────────────────────────────────────

void TstSchemaConstraints::test_fkEnforced_credentialRequiresUser()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO credentials VALUES (?,?,3,65536,4,32,'aaa','bbb',?);"));
    q.addBindValue(QStringLiteral("nonexistent-user-id"));
    q.addBindValue(QStringLiteral("argon2id"));
    q.addBindValue(nowUtc());
    QVERIFY2(!q.exec(), "Credential must reference an existing user");
}

void TstSchemaConstraints::test_fkEnforced_termCardRequiresMember()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO term_cards VALUES (?,?,'2025-01-01','2026-01-01',1,?);"));
    q.addBindValue(newUuid());
    q.addBindValue(QStringLiteral("nonexistent-member-id"));
    q.addBindValue(nowUtc());
    QVERIFY2(!q.exec(), "Term card must reference an existing member");
}

// ── Duplicate window query ───────────────────────────────────────────────────

void TstSchemaConstraints::test_duplicateWindowQuery_detectsWithin30s()
{
    exec(QStringLiteral("INSERT INTO members (id, member_id, member_id_hash, barcode_encrypted, "
                         "mobile_encrypted, name_encrypted, deleted, created_at, updated_at) "
                         "VALUES ('m-dup','MID-DUP','h-mid-dup','e','e','e',0,?,?);"),
         {nowUtc(), nowUtc()});
    // Insert a successful check-in at "now"
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    exec(QStringLiteral("INSERT INTO checkin_attempts VALUES ('ca-dup','m-dup','S99','u-seed','Success',?,NULL,NULL);"),
         {now});

    // Query for duplicate within 30 seconds — should find it
    const QString threshold = QDateTime::currentDateTimeUtc().addSecs(-30).toString(Qt::ISODateWithMs);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM checkin_attempts"
        " WHERE member_id = 'm-dup' AND session_id = 'S99'"
        " AND status = 'Success'"
        " AND attempted_at >= ?;"
    ));
    q.addBindValue(threshold);
    QVERIFY(q.exec() && q.next());
    QVERIFY2(q.value(0).toInt() > 0, "Should detect the duplicate within 30-second window");
}

void TstSchemaConstraints::test_duplicateWindowQuery_allowsAfter30s()
{
    exec(QStringLiteral("INSERT INTO members (id, member_id, member_id_hash, barcode_encrypted, "
                         "mobile_encrypted, name_encrypted, deleted, created_at, updated_at) "
                         "VALUES ('m-old','MID-OLD','h-mid-old','e','e','e',0,?,?);"),
         {nowUtc(), nowUtc()});
    // Insert a successful check-in 60 seconds ago
    const QString old = QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODateWithMs);
    exec(QStringLiteral("INSERT INTO checkin_attempts VALUES ('ca-old','m-old','S88','u-seed','Success',?,NULL,NULL);"),
         {old});

    // Query for duplicate within 30 seconds of now — should NOT find the old entry
    const QString threshold = QDateTime::currentDateTimeUtc().addSecs(-30).toString(Qt::ISODateWithMs);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM checkin_attempts"
        " WHERE member_id = 'm-old' AND session_id = 'S88'"
        " AND status = 'Success'"
        " AND attempted_at >= ?;"
    ));
    q.addBindValue(threshold);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

// ── Audit chain head — single-row enforcement ─────────────────────────────────

void TstSchemaConstraints::test_auditChainHead_singleRow()
{
    // The chain_head table already has id=1 (inserted by applyBaseSchema).
    // Inserting a second row with id != 1 should be rejected by the CHECK constraint.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO audit_chain_head VALUES (2, NULL, '');"));
    QVERIFY2(!q.exec(), "audit_chain_head must only allow id=1");

    // Updating id=1 must succeed (normal chain-head update)
    QVERIFY(exec(QStringLiteral("UPDATE audit_chain_head SET last_entry_hash='abc' WHERE id=1;")));
}

QTEST_GUILESS_MAIN(TstSchemaConstraints)
#include "tst_schema_constraints.moc"
