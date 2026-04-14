#pragma once
// IMemberRepository.h — ProctorOps
// Pure interface for member, entitlement card, and freeze record data access.
// Sensitive fields are stored encrypted; decryption is caller's responsibility
// (via CryptoService / AesGcmCipher).

#include "models/Member.h"
#include "utils/Result.h"
#include <QString>
#include <QList>
#include <optional>

class IMemberRepository {
public:
    virtual ~IMemberRepository() = default;

    // ── Members ────────────────────────────────────────────────────────────
    virtual Result<Member>         insertMember(const Member& member)                            = 0;
    virtual Result<Member>         findMemberById(const QString& memberId)                       = 0;
    // Lookup methods accept plaintext identifiers from operators.
    // Implementations persist member_id encrypted at rest and use a deterministic
    // hash index for direct member-id lookup.
    virtual Result<Member>         findMemberByMemberId(const QString& humanId)                  = 0;
    virtual Result<Member>         findMemberByBarcode(const QString& barcode)                   = 0;
    virtual Result<Member>         findMemberByMobileNormalized(const QString& mobile)           = 0;
    virtual Result<void>           updateMember(const Member& member)                            = 0;
    virtual Result<void>           softDeleteMember(const QString& memberId)                     = 0;
    // Anonymize: replace encrypted PII fields with fixed-length tombstones.
    virtual Result<void>           anonymizeMember(const QString& memberId)                      = 0;

    // ── Term cards ─────────────────────────────────────────────────────────
    virtual Result<TermCard>        insertTermCard(const TermCard& card)                         = 0;
    virtual Result<QList<TermCard>> getActiveTermCards(const QString& memberId,
                                                        const QDate& onDate)                     = 0;
    virtual Result<QList<TermCard>> getAllTermCards(const QString& memberId)                      = 0;

    // ── Punch cards ────────────────────────────────────────────────────────
    virtual Result<PunchCard>        insertPunchCard(const PunchCard& card)                      = 0;
    // Atomic deduction: subtracts 1 from currentBalance; fails if balance == 0.
    // Must execute inside a caller-managed transaction for atomicity.
    virtual Result<PunchCard>        deductSession(const QString& punchCardId)                   = 0;
    virtual Result<PunchCard>        getPunchCard(const QString& id)                             = 0;
    virtual Result<QList<PunchCard>> getActivePunchCards(const QString& memberId)                = 0;
    // Restore balance by 1 (used by correction approval).
    virtual Result<void>             restoreSession(const QString& punchCardId)                  = 0;

    // ── Freeze records ─────────────────────────────────────────────────────
    virtual Result<MemberFreezeRecord>           insertFreezeRecord(const MemberFreezeRecord&)   = 0;
    virtual Result<std::optional<MemberFreezeRecord>> getActiveFreezeRecord(const QString& memberId) = 0;
    virtual Result<QList<MemberFreezeRecord>>    listRecentFreezeRecords(int limit = 50)         = 0;
    virtual Result<void>                         thawMember(const QString& memberId,
                                                              const QString& thawedByUserId)     = 0;
};
