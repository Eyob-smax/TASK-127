#pragma once
// MemberRepository.h — ProctorOps
// Concrete SQLite implementation of IMemberRepository.
// Manages members (with encrypted PII), term cards, punch cards, and freeze records.

#include "IMemberRepository.h"
#include <QSqlDatabase>
#include <functional>

class MemberRepository : public IMemberRepository {
public:
    explicit MemberRepository(QSqlDatabase& db);

    // ── Members ────────────────────────────────────────────────────────────
    Result<Member>         insertMember(const Member& member) override;
    Result<Member>         findMemberById(const QString& memberId) override;
    Result<Member>         findMemberByMemberId(const QString& humanId) override;
    Result<Member>         findMemberByBarcode(const QString& barcode) override;
    Result<Member>         findMemberByMobileNormalized(const QString& mobile) override;
    Result<void>           updateMember(const Member& member) override;
    Result<void>           softDeleteMember(const QString& memberId) override;
    Result<void>           anonymizeMember(const QString& memberId) override;

    // ── Term cards ─────────────────────────────────────────────────────────
    Result<TermCard>        insertTermCard(const TermCard& card) override;
    Result<QList<TermCard>> getActiveTermCards(const QString& memberId,
                                                const QDate& onDate) override;
    Result<QList<TermCard>> getAllTermCards(const QString& memberId) override;

    // ── Punch cards ────────────────────────────────────────────────────────
    Result<PunchCard>        insertPunchCard(const PunchCard& card) override;
    Result<PunchCard>        deductSession(const QString& punchCardId) override;
    Result<PunchCard>        getPunchCard(const QString& id) override;
    Result<QList<PunchCard>> getActivePunchCards(const QString& memberId) override;
    Result<void>             restoreSession(const QString& punchCardId) override;

    // ── Freeze records ─────────────────────────────────────────────────────
    Result<MemberFreezeRecord>                insertFreezeRecord(const MemberFreezeRecord& r) override;
    Result<std::optional<MemberFreezeRecord>> getActiveFreezeRecord(const QString& memberId) override;
    Result<QList<MemberFreezeRecord>>         listRecentFreezeRecords(int limit = 50) override;
    Result<void>                              thawMember(const QString& memberId,
                                                          const QString& thawedByUserId) override;

    /// Set a decryptor callback for encrypted PII field searching.
    /// The callback takes ciphertext and returns plaintext (or empty on failure).
    using Decryptor = std::function<QString(const QString&, const QString&)>;
    void setDecryptor(Decryptor decryptor);

    /// Set an encryptor callback for encrypted member identifier persistence.
    /// The callback takes a field name + plaintext and returns ciphertext base64.
    using Encryptor = std::function<QString(const QString&, const QString&)>;
    void setEncryptor(Encryptor encryptor);

private:
    QSqlDatabase& m_db;
    Decryptor m_decryptor;
    Encryptor m_encryptor;

    Member rowToMember(const QSqlQuery& q) const;
    static TermCard rowToTermCard(const QSqlQuery& q);
    static PunchCard rowToPunchCard(const QSqlQuery& q);
    static MemberFreezeRecord rowToFreezeRecord(const QSqlQuery& q);
    static QString memberIdHash(const QString& memberId);

    Result<Member> findMemberByEncryptedField(const QString& plaintext,
                                                const QString& fieldName);
};
