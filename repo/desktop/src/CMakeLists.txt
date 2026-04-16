# src/CMakeLists.txt
# ProctorOps application executable target.
#
# This file defines the executable target and include structure.
# Subdirectories are added as their source files are authored.

# ── Executable entry point ────────────────────────────────────────────────────
qt_add_executable(ProctorOps
    main.cpp

    # ── Domain model implementation ──────────────────────────────────────────
    models/CommonTypes.cpp

    # ── Crypto primitives ────────────────────────────────────────────────────
    crypto/SecureRandom.cpp
    crypto/Argon2idHasher.cpp
    crypto/AesGcmCipher.cpp
    crypto/Ed25519Verifier.cpp
    crypto/HashChain.cpp
    crypto/KeyStore.cpp

    # ── Shared utilities ─────────────────────────────────────────────────────
    utils/Logger.cpp
    utils/ClipboardGuard.cpp
    utils/MaskingPolicy.cpp
    utils/CaptchaGenerator.cpp
    utils/ErrorFormatter.cpp

    # ── Crypto extensions ────────────────────────────────────────────────────
    crypto/Ed25519Signer.cpp

    # ── Concrete repositories ────────────────────────────────────────────────
    repositories/UserRepository.cpp
    repositories/AuditRepository.cpp
    repositories/SyncRepository.cpp
    repositories/UpdateRepository.cpp

    # ── Concrete repositories (domain) ─────────────────────────────────────
    repositories/QuestionRepository.cpp
    repositories/KnowledgePointRepository.cpp
    repositories/MemberRepository.cpp
    repositories/CheckInRepository.cpp
    repositories/IngestionRepository.cpp

    # ── Services ─────────────────────────────────────────────────────────────
    services/AuthService.cpp
    services/AuditService.cpp
    services/PackageVerifier.cpp
    services/QuestionService.cpp
    services/CheckInService.cpp
    services/IngestionService.cpp
    services/SyncService.cpp
    services/DataSubjectService.cpp
    services/UpdateService.cpp

    # ── Scheduler ────────────────────────────────────────────────────────────
    scheduler/JobScheduler.cpp

    # ── Widgets ──────────────────────────────────────────────────────────────
    widgets/BarcodeInput.cpp

    # ── Application shell ────────────────────────────────────────────────────
    app/Application.cpp
    app/AppContext.cpp
    app/AppBootstrap.cpp
    app/AppSettings.cpp
    app/ActionRouter.cpp
    app/WorkspaceState.cpp

    # ── Windows ───────────────────────────────────────────────────────────────
    windows/MainShell.cpp
    windows/LoginWindow.cpp
    windows/CheckInWindow.cpp
    windows/QuestionBankWindow.cpp
    windows/AuditViewerWindow.cpp
    windows/SyncWindow.cpp
    windows/DataSubjectWindow.cpp
    windows/SecurityAdminWindow.cpp
    windows/UpdateWindow.cpp
    windows/IngestionMonitorWindow.cpp

    # ── Dialogs ───────────────────────────────────────────────────────────────
    dialogs/CommandPaletteDialog.cpp
    dialogs/StepUpDialog.cpp
    dialogs/QuestionEditorDialog.cpp

    # ── Tray ──────────────────────────────────────────────────────────────────
    tray/TrayManager.cpp

    # ── Additional widgets ────────────────────────────────────────────────────
    widgets/MaskedFieldWidget.cpp

    # ── Additional utilities ──────────────────────────────────────────────────
    utils/Migration.cpp
    utils/PerformanceObserver.cpp
)

target_include_directories(ProctorOps PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ProctorOps PRIVATE
    ProctorOpsCore
    Qt6::Widgets
    Qt6::Sql
)

# Windows-specific build flags
if(WIN32)
    # Suppress console window in release builds
    set_target_properties(ProctorOps PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>
    )
    # Psapi.lib for GetProcessMemoryInfo (PerformanceObserver)
    target_link_libraries(ProctorOps PRIVATE Psapi)
endif()
