#pragma once
// AppContext.h — ProctorOps
// Aggregate of all initialized infrastructure (crypto, repositories, services)
// threaded through the application after startup.
//
// AppContext is constructed once in main() after Application::initialize().
// Ownership lives in AppContext; MainShell and its child windows receive a
// non-owning pointer. The session field is written by the login flow and
// read by any window that needs the current operator identity.

#include "models/User.h"
#include <memory>

// Forward-declare all dependencies to keep this header light.
class KeyStore;
class AesGcmCipher;
class UserRepository;
class AuditRepository;
class QuestionRepository;
class KnowledgePointRepository;
class MemberRepository;
class CheckInRepository;
class IngestionRepository;
class SyncRepository;
class UpdateRepository;
class AuditService;
class AuthService;
class QuestionService;
class CheckInService;
class IngestionService;
class SyncService;
class DataSubjectService;
class UpdateService;
class JobScheduler;
class PackageVerifier;

struct AppContext {
    ~AppContext();

    // ── Crypto ────────────────────────────────────────────────────────────────
    std::unique_ptr<KeyStore>                 keyStore;
    std::unique_ptr<AesGcmCipher>             cipher;

    // ── Repositories ──────────────────────────────────────────────────────────
    std::unique_ptr<UserRepository>           userRepo;
    std::unique_ptr<AuditRepository>          auditRepo;
    std::unique_ptr<QuestionRepository>       questionRepo;
    std::unique_ptr<KnowledgePointRepository> kpRepo;
    std::unique_ptr<MemberRepository>         memberRepo;
    std::unique_ptr<CheckInRepository>        checkInRepo;
    std::unique_ptr<IngestionRepository>      ingestionRepo;
    std::unique_ptr<SyncRepository>           syncRepo;
    std::unique_ptr<UpdateRepository>         updateRepo;
    std::unique_ptr<PackageVerifier>          packageVerifier;

    // ── Services ──────────────────────────────────────────────────────────────
    std::unique_ptr<AuditService>             auditService;
    std::unique_ptr<AuthService>              authService;
    std::unique_ptr<QuestionService>          questionService;
    std::unique_ptr<CheckInService>           checkInService;
    std::unique_ptr<IngestionService>         ingestionService;
    std::unique_ptr<SyncService>              syncService;
    std::unique_ptr<DataSubjectService>       dataSubjectService;
    std::unique_ptr<UpdateService>            updateService;
    std::unique_ptr<JobScheduler>             jobScheduler;

    // ── Session state (written by login flow) ─────────────────────────────────
    UserSession session;
    bool        authenticated{false};
};
