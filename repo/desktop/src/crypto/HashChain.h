#pragma once
// HashChain.h — ProctorOps
// SHA-256 hash computation for tamper-evident audit chain entries.
// Provides canonical serialization of AuditEntry fields and general SHA-256.

#include "models/Audit.h"
#include <QByteArray>
#include <QString>

class HashChain {
public:
    HashChain() = delete;

    /// Compute the entry hash for an AuditEntry using its canonical serialization.
    /// The canonical form includes: id, timestamp, actorUserId, eventType,
    /// entityType, entityId, beforePayloadJson, afterPayloadJson, previousEntryHash.
    /// Returns lowercase SHA-256 hex string.
    [[nodiscard]] static QString computeEntryHash(const AuditEntry& entry);

    /// Compute SHA-256 of arbitrary data. Returns lowercase hex string.
    [[nodiscard]] static QString computeSha256(const QByteArray& data);
};
