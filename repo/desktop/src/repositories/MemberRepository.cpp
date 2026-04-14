// MemberRepository.cpp — ProctorOps
// Concrete SQLite implementation for members, term cards, punch cards,
// and freeze records. Encrypted PII lookup uses scan-and-decrypt approach
// (acceptable for < 10K members per site; HMAC index could be added for scale).

#include "MemberRepository.h"

#include <QCryptographicHash>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>

MemberRepository::MemberRepository(QSqlDatabase& db)
    : m_db(db)
{
}

void MemberRepository::setDecryptor(Decryptor decryptor)
{
    m_decryptor = std::move(decryptor);
}

void MemberRepository::setEncryptor(Encryptor encryptor)
{
    m_encryptor = std::move(encryptor);
}

// ── Row mapping helpers ──────────────────────────────────────────────────────

Member MemberRepository::rowToMember(const QSqlQuery& q) const
{
    Member m;
    m.id               = q.value(0).toString();
    const QString storedMemberId = q.value(1).toString();
    m.memberId         = storedMemberId;
    m.barcodeEncrypted = q.value(2).toString();
    m.mobileEncrypted  = q.value(3).toString();
    m.nameEncrypted    = q.value(4).toString();
    m.deleted          = q.value(5).toBool();
    m.createdAt        = QDateTime::fromString(q.value(6).toString(), Qt::ISODateWithMs);
    m.updatedAt        = QDateTime::fromString(q.value(7).toString(), Qt::ISODateWithMs);

    if (m_decryptor
        && !storedMemberId.isEmpty()
        && storedMemberId != QStringLiteral("[ANONYMIZED]")) {
        const QString decrypted = m_decryptor(QStringLiteral("member_id"), storedMemberId);
        if (!decrypted.isEmpty())
            m.memberId = decrypted;
    }

    return m;
}

QString MemberRepository::memberIdHash(const QString& memberId)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(memberId.trimmed().toUtf8(), QCryptographicHash::Sha256).toHex());
}

TermCard MemberRepository::rowToTermCard(const QSqlQuery& q)
{
    TermCard tc;
    tc.id        = q.value(0).toString();
    tc.memberId  = q.value(1).toString();
    tc.termStart = QDate::fromString(q.value(2).toString(), Qt::ISODate);
    tc.termEnd   = QDate::fromString(q.value(3).toString(), Qt::ISODate);
    tc.active    = q.value(4).toBool();
    tc.createdAt = QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
    return tc;
}

PunchCard MemberRepository::rowToPunchCard(const QSqlQuery& q)
{
    PunchCard pc;
    pc.id             = q.value(0).toString();
    pc.memberId       = q.value(1).toString();
    pc.productCode    = q.value(2).toString();
    pc.initialBalance = q.value(3).toInt();
    pc.currentBalance = q.value(4).toInt();
    pc.createdAt      = QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
    pc.updatedAt      = QDateTime::fromString(q.value(6).toString(), Qt::ISODateWithMs);
    return pc;
}

MemberFreezeRecord MemberRepository::rowToFreezeRecord(const QSqlQuery& q)
{
    MemberFreezeRecord fr;
    fr.id             = q.value(0).toString();
    fr.memberId       = q.value(1).toString();
    fr.reason         = q.value(2).toString();
    fr.frozenByUserId = q.value(3).toString();
    fr.frozenAt       = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
    fr.thawedByUserId = q.value(5).toString();
    fr.thawedAt       = q.value(6).isNull() ? QDateTime()
                          : QDateTime::fromString(q.value(6).toString(), Qt::ISODateWithMs);
    return fr;
}

// ── Members ──────────────────────────────────────────────────────────────────

Result<Member> MemberRepository::insertMember(const Member& member)
{
    QString storedMemberId = member.memberId;
    if (m_encryptor && !member.memberId.isEmpty()) {
        const QString encrypted = m_encryptor(QStringLiteral("member_id"), member.memberId);
        if (encrypted.isEmpty()) {
            return Result<Member>::err(
                ErrorCode::EncryptionFailed,
                QStringLiteral("Failed to encrypt member identifier"));
        }
        storedMemberId = encrypted;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO members "
        "(id, member_id, member_id_hash, barcode_encrypted, mobile_encrypted, name_encrypted, "
        " deleted, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(member.id);
    q.addBindValue(storedMemberId);
    q.addBindValue(memberIdHash(member.memberId));
    q.addBindValue(member.barcodeEncrypted);
    q.addBindValue(member.mobileEncrypted);
    q.addBindValue(member.nameEncrypted);
    q.addBindValue(member.deleted ? 1 : 0);
    q.addBindValue(member.createdAt.toString(Qt::ISODateWithMs));
    q.addBindValue(member.updatedAt.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<Member>::err(ErrorCode::DbError, q.lastError().text());

    return Result<Member>::ok(member);
}

Result<Member> MemberRepository::findMemberById(const QString& memberId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, barcode_encrypted, mobile_encrypted, name_encrypted, "
        "       deleted, created_at, updated_at "
        "FROM members WHERE id = ?"));
    q.addBindValue(memberId);

    if (!q.exec())
        return Result<Member>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<Member>::err(ErrorCode::NotFound);

    return Result<Member>::ok(rowToMember(q));
}

Result<Member> MemberRepository::findMemberByMemberId(const QString& humanId)
{
    const QString expectedHash = memberIdHash(humanId);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, barcode_encrypted, mobile_encrypted, name_encrypted, "
        "       deleted, created_at, updated_at "
        "FROM members WHERE member_id_hash = ? AND deleted = 0"));
    q.addBindValue(expectedHash);

    if (!q.exec())
        return Result<Member>::err(ErrorCode::DbError, q.lastError().text());

    if (q.next())
        return Result<Member>::ok(rowToMember(q));

    if (!m_decryptor) {
        QSqlQuery legacy(m_db);
        legacy.prepare(QStringLiteral(
            "SELECT id, member_id, barcode_encrypted, mobile_encrypted, name_encrypted, "
            "       deleted, created_at, updated_at "
            "FROM members WHERE member_id = ? AND deleted = 0"));
        legacy.addBindValue(humanId);
        if (!legacy.exec())
            return Result<Member>::err(ErrorCode::DbError, legacy.lastError().text());
        if (!legacy.next())
            return Result<Member>::err(ErrorCode::NotFound);
        return Result<Member>::ok(rowToMember(legacy));
    }

    QSqlQuery fallback(m_db);
    fallback.prepare(QStringLiteral(
        "SELECT id, member_id, barcode_encrypted, mobile_encrypted, name_encrypted, "
        "       deleted, created_at, updated_at "
        "FROM members "
        "WHERE deleted = 0 AND (member_id_hash IS NULL OR member_id_hash = '')"));
    if (!fallback.exec())
        return Result<Member>::err(ErrorCode::DbError, fallback.lastError().text());

    while (fallback.next()) {
        const Member member = rowToMember(fallback);
        if (member.memberId == humanId)
            return Result<Member>::ok(member);
    }

    return Result<Member>::err(ErrorCode::NotFound);
}

Result<Member> MemberRepository::findMemberByEncryptedField(const QString& plaintext,
                                                              const QString& fieldName)
{
    if (!m_decryptor)
        return Result<Member>::err(ErrorCode::ValidationFailed,
                                    QStringLiteral("Decryptor not configured"));

    // Scan all non-deleted members and compare decrypted field values.
    // Acceptable for < 10K members per site.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, barcode_encrypted, mobile_encrypted, name_encrypted, "
        "       deleted, created_at, updated_at "
        "FROM members WHERE deleted = 0"));

    if (!q.exec())
        return Result<Member>::err(ErrorCode::DbError, q.lastError().text());

    while (q.next()) {
        Member m = rowToMember(q);
        QString ciphertext;
        if (fieldName == QStringLiteral("barcode"))
            ciphertext = m.barcodeEncrypted;
        else if (fieldName == QStringLiteral("mobile"))
            ciphertext = m.mobileEncrypted;
        else
            continue;

        if (ciphertext.isEmpty())
            continue;

        QString decrypted = m_decryptor(fieldName, ciphertext);
        if (decrypted == plaintext)
            return Result<Member>::ok(std::move(m));
    }

    return Result<Member>::err(ErrorCode::NotFound);
}

Result<Member> MemberRepository::findMemberByBarcode(const QString& barcode)
{
    return findMemberByEncryptedField(barcode, QStringLiteral("barcode"));
}

Result<Member> MemberRepository::findMemberByMobileNormalized(const QString& mobile)
{
    return findMemberByEncryptedField(mobile, QStringLiteral("mobile"));
}

Result<void> MemberRepository::updateMember(const Member& member)
{
    QString storedMemberId = member.memberId;
    if (m_encryptor && !member.memberId.isEmpty()) {
        const QString encrypted = m_encryptor(QStringLiteral("member_id"), member.memberId);
        if (encrypted.isEmpty()) {
            return Result<void>::err(
                ErrorCode::EncryptionFailed,
                QStringLiteral("Failed to encrypt member identifier"));
        }
        storedMemberId = encrypted;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE members SET "
        "member_id = ?, member_id_hash = ?, barcode_encrypted = ?, mobile_encrypted = ?, "
        "name_encrypted = ?, deleted = ?, updated_at = ? "
        "WHERE id = ?"));
    q.addBindValue(storedMemberId);
    q.addBindValue(memberIdHash(member.memberId));
    q.addBindValue(member.barcodeEncrypted);
    q.addBindValue(member.mobileEncrypted);
    q.addBindValue(member.nameEncrypted);
    q.addBindValue(member.deleted ? 1 : 0);
    q.addBindValue(member.updatedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(member.id);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::NotFound);

    return Result<void>::ok();
}

Result<void> MemberRepository::softDeleteMember(const QString& memberId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE members SET deleted = 1, updated_at = ? WHERE id = ?"));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.addBindValue(memberId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::NotFound);

    return Result<void>::ok();
}

Result<void> MemberRepository::anonymizeMember(const QString& memberId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE members SET "
        "member_id = '[ANONYMIZED]', "
        "member_id_hash = NULL, "
        "barcode_encrypted = '[ANONYMIZED]', "
        "mobile_encrypted = '[ANONYMIZED]', "
        "name_encrypted = '[ANONYMIZED]', "
        "deleted = 1, updated_at = ? "
        "WHERE id = ?"));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.addBindValue(memberId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::NotFound);

    return Result<void>::ok();
}

// ── Term cards ───────────────────────────────────────────────────────────────

Result<TermCard> MemberRepository::insertTermCard(const TermCard& card)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO term_cards (id, member_id, term_start, term_end, active, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(card.id);
    q.addBindValue(card.memberId);
    q.addBindValue(card.termStart.toString(Qt::ISODate));
    q.addBindValue(card.termEnd.toString(Qt::ISODate));
    q.addBindValue(card.active ? 1 : 0);
    q.addBindValue(card.createdAt.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<TermCard>::err(ErrorCode::DbError, q.lastError().text());

    return Result<TermCard>::ok(card);
}

Result<QList<TermCard>> MemberRepository::getActiveTermCards(const QString& memberId,
                                                               const QDate& onDate)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, term_start, term_end, active, created_at "
        "FROM term_cards "
        "WHERE member_id = ? AND active = 1 AND term_start <= ? AND term_end >= ?"));
    q.addBindValue(memberId);
    q.addBindValue(onDate.toString(Qt::ISODate));
    q.addBindValue(onDate.toString(Qt::ISODate));

    if (!q.exec())
        return Result<QList<TermCard>>::err(ErrorCode::DbError, q.lastError().text());

    QList<TermCard> results;
    while (q.next())
        results.append(rowToTermCard(q));

    return Result<QList<TermCard>>::ok(std::move(results));
}

Result<QList<TermCard>> MemberRepository::getAllTermCards(const QString& memberId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, term_start, term_end, active, created_at "
        "FROM term_cards WHERE member_id = ? ORDER BY term_start DESC"));
    q.addBindValue(memberId);

    if (!q.exec())
        return Result<QList<TermCard>>::err(ErrorCode::DbError, q.lastError().text());

    QList<TermCard> results;
    while (q.next())
        results.append(rowToTermCard(q));

    return Result<QList<TermCard>>::ok(std::move(results));
}

// ── Punch cards ──────────────────────────────────────────────────────────────

Result<PunchCard> MemberRepository::insertPunchCard(const PunchCard& card)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO punch_cards "
        "(id, member_id, product_code, initial_balance, current_balance, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(card.id);
    q.addBindValue(card.memberId);
    q.addBindValue(card.productCode);
    q.addBindValue(card.initialBalance);
    q.addBindValue(card.currentBalance);
    q.addBindValue(card.createdAt.toString(Qt::ISODateWithMs));
    q.addBindValue(card.updatedAt.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<PunchCard>::err(ErrorCode::DbError, q.lastError().text());

    return Result<PunchCard>::ok(card);
}

Result<PunchCard> MemberRepository::deductSession(const QString& punchCardId)
{
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE punch_cards "
        "SET current_balance = current_balance - 1, updated_at = ? "
        "WHERE id = ? AND current_balance > 0"));
    q.addBindValue(now);
    q.addBindValue(punchCardId);

    if (!q.exec())
        return Result<PunchCard>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<PunchCard>::err(ErrorCode::PunchCardExhausted,
                                       QStringLiteral("Punch card balance is zero"));

    return getPunchCard(punchCardId);
}

Result<PunchCard> MemberRepository::getPunchCard(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, product_code, initial_balance, current_balance, "
        "       created_at, updated_at "
        "FROM punch_cards WHERE id = ?"));
    q.addBindValue(id);

    if (!q.exec())
        return Result<PunchCard>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<PunchCard>::err(ErrorCode::NotFound);

    return Result<PunchCard>::ok(rowToPunchCard(q));
}

Result<QList<PunchCard>> MemberRepository::getActivePunchCards(const QString& memberId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, product_code, initial_balance, current_balance, "
        "       created_at, updated_at "
        "FROM punch_cards WHERE member_id = ? AND current_balance > 0"));
    q.addBindValue(memberId);

    if (!q.exec())
        return Result<QList<PunchCard>>::err(ErrorCode::DbError, q.lastError().text());

    QList<PunchCard> results;
    while (q.next())
        results.append(rowToPunchCard(q));

    return Result<QList<PunchCard>>::ok(std::move(results));
}

Result<void> MemberRepository::restoreSession(const QString& punchCardId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE punch_cards "
        "SET current_balance = current_balance + 1, updated_at = ? "
        "WHERE id = ? AND current_balance < initial_balance"));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.addBindValue(punchCardId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("Cannot restore: balance already at initial"));

    return Result<void>::ok();
}

// ── Freeze records ───────────────────────────────────────────────────────────

Result<MemberFreezeRecord> MemberRepository::insertFreezeRecord(const MemberFreezeRecord& r)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO member_freeze_records "
        "(id, member_id, reason, frozen_by_user_id, frozen_at, thawed_by_user_id, thawed_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(r.id);
    q.addBindValue(r.memberId);
    q.addBindValue(r.reason);
    q.addBindValue(r.frozenByUserId);
    q.addBindValue(r.frozenAt.toString(Qt::ISODateWithMs));
    q.addBindValue(r.thawedByUserId.isEmpty() ? QVariant() : r.thawedByUserId);
    q.addBindValue(r.thawedAt.isNull() ? QVariant() : r.thawedAt.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<MemberFreezeRecord>::err(ErrorCode::DbError, q.lastError().text());

    return Result<MemberFreezeRecord>::ok(r);
}

Result<std::optional<MemberFreezeRecord>>
MemberRepository::getActiveFreezeRecord(const QString& memberId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, reason, frozen_by_user_id, frozen_at, "
        "       thawed_by_user_id, thawed_at "
        "FROM member_freeze_records "
        "WHERE member_id = ? AND thawed_at IS NULL "
        "ORDER BY frozen_at DESC LIMIT 1"));
    q.addBindValue(memberId);

    if (!q.exec())
        return Result<std::optional<MemberFreezeRecord>>::err(
            ErrorCode::DbError, q.lastError().text());

    if (!q.next())
        return Result<std::optional<MemberFreezeRecord>>::ok(std::nullopt);

    return Result<std::optional<MemberFreezeRecord>>::ok(rowToFreezeRecord(q));
}

Result<QList<MemberFreezeRecord>> MemberRepository::listRecentFreezeRecords(int limit)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, reason, frozen_by_user_id, frozen_at, "
        "       thawed_by_user_id, thawed_at "
        "FROM member_freeze_records "
        "ORDER BY frozen_at DESC LIMIT ?"));
    q.addBindValue(limit);

    if (!q.exec())
        return Result<QList<MemberFreezeRecord>>::err(ErrorCode::DbError, q.lastError().text());

    QList<MemberFreezeRecord> records;
    while (q.next())
        records.append(rowToFreezeRecord(q));

    return Result<QList<MemberFreezeRecord>>::ok(std::move(records));
}

Result<void> MemberRepository::thawMember(const QString& memberId,
                                            const QString& thawedByUserId)
{
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE member_freeze_records "
        "SET thawed_by_user_id = ?, thawed_at = ? "
        "WHERE member_id = ? AND thawed_at IS NULL"));
    q.addBindValue(thawedByUserId);
    q.addBindValue(now);
    q.addBindValue(memberId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}
