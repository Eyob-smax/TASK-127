cmake_minimum_required(VERSION 3.28)

project(ProctorOps
    VERSION 0.1.0
    DESCRIPTION "ProctorOps Offline Assessment Operations Console"
    LANGUAGES CXX
)

# ── Language standard ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Qt auto-tooling ──────────────────────────────────────────────────────────
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Qt 6 ─────────────────────────────────────────────────────────────────────
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Widgets
    Sql
    Concurrent
    StateMachine
    Test
)

qt_standard_project_setup()

# ── External dependencies ─────────────────────────────────────────────────────

# OpenSSL 3.x — AES-256-GCM, Ed25519, HKDF-SHA256
find_package(OpenSSL 3.0 REQUIRED)

# Argon2id — password hashing
# libargon2-dev provides pkg-config support on Linux; on Windows use vcpkg or conan.
find_path(ARGON2_INCLUDE_DIR
    NAMES argon2.h
    PATHS
        /usr/include
        /usr/local/include
        ${ARGON2_ROOT}/include
    REQUIRED
)
find_library(ARGON2_LIBRARY
    NAMES argon2 libargon2
    PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/local/lib
        ${ARGON2_ROOT}/lib
    REQUIRED
)

# SQLite 3.39+ — Qt SQL driver uses system SQLite if available
find_package(SQLite3 3.39 QUIET)
if(NOT SQLite3_FOUND)
    # Qt ships a bundled SQLite; fall back to it if system SQLite is too old.
    message(STATUS "System SQLite not found or too old — relying on Qt's bundled SQLite")
endif()

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic -Werror
        -Wno-unused-parameter   # Qt slots commonly leave params unnamed
    )
endif()

# ── Shared core library ───────────────────────────────────────────────────────
# Core domain/services/repositories are built once here and reused by tests.
add_library(ProctorOpsCore STATIC
    src/models/CommonTypes.cpp

    src/crypto/SecureRandom.cpp
    src/crypto/Argon2idHasher.cpp
    src/crypto/AesGcmCipher.cpp
    src/crypto/Ed25519Verifier.cpp
    src/crypto/HashChain.cpp
    src/crypto/KeyStore.cpp
    src/crypto/Ed25519Signer.cpp

    src/utils/Logger.cpp
    src/utils/ClipboardGuard.cpp
    src/utils/MaskingPolicy.cpp
    src/utils/CaptchaGenerator.cpp
    src/utils/ErrorFormatter.cpp
    src/utils/Migration.cpp
    src/utils/PerformanceObserver.cpp

    src/repositories/UserRepository.cpp
    src/repositories/AuditRepository.cpp
    src/repositories/SyncRepository.cpp
    src/repositories/UpdateRepository.cpp
    src/repositories/QuestionRepository.cpp
    src/repositories/KnowledgePointRepository.cpp
    src/repositories/MemberRepository.cpp
    src/repositories/CheckInRepository.cpp
    src/repositories/IngestionRepository.cpp

    src/services/AuthService.cpp
    src/services/AuditService.cpp
    src/services/PackageVerifier.cpp
    src/services/QuestionService.cpp
    src/services/CheckInService.cpp
    src/services/IngestionService.cpp
    src/services/SyncService.cpp
    src/services/DataSubjectService.cpp
    src/services/UpdateService.cpp

    src/scheduler/JobScheduler.cpp

    src/app/AppContext.cpp
    src/app/AppSettings.cpp
    src/app/ActionRouter.cpp
    src/app/WorkspaceState.cpp
)
target_include_directories(ProctorOpsCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ARGON2_INCLUDE_DIR}
)
target_link_libraries(ProctorOpsCore PUBLIC
    Qt6::Core
    Qt6::Sql
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::StateMachine
    OpenSSL::SSL
    OpenSSL::Crypto
    ${ARGON2_LIBRARY}
)

# ── Application executable ────────────────────────────────────────────────────
add_subdirectory(src)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(unit_tests)
add_subdirectory(api_tests)

# ── Installation (native Windows — cmake --install) ───────────────────────────
include(GNUInstallDirs)
install(TARGETS ProctorOps
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ── CPack placeholder (no signed .msi required — see docs/design.md §2) ───────
# CPack configuration is intentionally omitted. The signed .msi delivery
# artifact is explicitly not required for this project. Update/rollback
# logic uses .proctorpkg packages, not the install artifact itself.
# If a packaging step is added in the future, configure it here.

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "ProctorOps ${PROJECT_VERSION}")
message(STATUS "  CXX standard  : C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Qt version    : ${Qt6_VERSION}")
message(STATUS "  OpenSSL       : ${OPENSSL_VERSION}")
message(STATUS "  Argon2 lib    : ${ARGON2_LIBRARY}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "")
