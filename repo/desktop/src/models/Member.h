#pragma once
// Member.h — ProctorOps
// Domain models for members, entitlement cards, freeze records, and entry identifiers.
// Sensitive fields (barcode, mobile, name) are stored AES-256-GCM encrypted.
// Masked display (last 4 digits) is the default in the UI layer.

#include <QString>
#include <QDate>
#include <QDateTime>

// ── Member ───────────────────────────────────────────────────────────────────
struct Member {
    QString   id;                   // UUID
    QString   memberId;             // human-readable ID, stored plaintext for lookup
    QString   barcodeEncrypted;     // AES-GCM ciphertext; see AesGcmCipher for format
    QString   mobileEncrypted;      // AES-GCM ciphertext; plaintext: (###) ###-####
    QString   nameEncrypted;        // AES-GCM ciphertext
    bool      deleted;              // soft-delete; anonymized on GDPR deletion
    QDateTime createdAt;
    QDateTime updatedAt;
};

// ── TermCard ─────────────────────────────────────────────────────────────────
// A time-bounded entitlement. Check-in is only allowed when term_start <= today <= term_end.
struct TermCard {
    QString  id;
    QString  memberId;
    QDate    termStart;   // inclusive
    QDate    termEnd;     // inclusive
    bool     active;
    QDateTime createdAt;
    // Invariant: termEnd > termStart
    // Expired if QDate::currentDate() > termEnd
};

// ── PunchCard ────────────────────────────────────────────────────────────────
// A consumable session entitlement. currentBalance decremented atomically per check-in.
struct PunchCard {
    QString  id;
    QString  memberId;
    QString  productCode;    // identifies which punch-card product this is
    int      initialBalance; // sessions granted at creation
    int      currentBalance; // remaining sessions; >= 0
    QDateTime createdAt;
    QDateTime updatedAt;
    // Invariant: currentBalance >= 0
    // Exhausted if currentBalance == 0
};

// ── MemberFreezeRecord ────────────────────────────────────────────────────────
// An active freeze blocks all check-ins for the member.
// Thawed by a security administrator with step-up verification.
struct MemberFreezeRecord {
    QString   id;
    QString   memberId;
    QString   reason;
    QString   frozenByUserId;
    QDateTime frozenAt;
    QString   thawedByUserId;  // empty if still frozen
    QDateTime thawedAt;        // isNull() if still frozen
};

// ── MemberIdentifier ─────────────────────────────────────────────────────────
// Input union for the three accepted check-in identifier types.
struct MemberIdentifier {
    enum class Type {
        Barcode,  // USB HID barcode scanner input
        MemberId, // manual typed member ID
        Mobile,   // typed mobile number, normalized to (###) ###-####
    };
    Type    type;
    QString value; // raw input value; normalization and lookup performed by CheckInService
};
