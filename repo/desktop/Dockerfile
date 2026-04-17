# ProctorOps — Build and Test Container
#
# Purpose: Reproducible build and headless Qt Test execution environment.
# This is NOT the production runtime. ProctorOps runs natively on Windows 11.
# Docker is used exclusively for CI-style build verification and headless test
# execution. Native Windows 11 GUI behavior is not verifiable inside Docker.
#
# Key notes for reviewers:
#   - QT_QPA_PLATFORM=offscreen: Qt renders headlessly — no display required.
#   - KeyStore: Windows DPAPI is unavailable on Linux. The Linux code path uses
#     a file-permission-protected key file (XOR-obfuscated). This is NOT
#     security-equivalent to DPAPI but is acceptable for test coverage.
#     See docs/questions.md §6 for full custody documentation.
#   - Performance targets (cold-start < 3 s, 7-day < 200 MB growth) require
#     manual verification on representative Windows 11 hardware; Docker does
#     not provide this proof.
#
# Base:  Ubuntu 22.04 LTS
# Qt:    6.6.3 via aqtinstall (pinned for reproducibility; satisfies Qt6 6.5+)
# CMake: 3.28+ via Kitware APT repository
# GCC:   12 (system package)


FROM ubuntu:22.04 AS base

# Use a more reliable Ubuntu mirror and clean stale lists
RUN apt-get clean && rm -rf /var/lib/apt/lists/* \
    && sed -i 's|http://archive.ubuntu.com/ubuntu|http://azure.archive.ubuntu.com/ubuntu|g' /etc/apt/sources.list \
    && sed -i 's|http://security.ubuntu.com/ubuntu|http://azure.archive.ubuntu.com/ubuntu|g' /etc/apt/sources.list

LABEL org.opencontainers.image.title="proctorops-build-test"
LABEL org.opencontainers.image.description="ProctorOps build and headless test container (Linux/Docker only, not the production runtime)"
LABEL org.opencontainers.image.source="repo/desktop/Dockerfile"

ARG DEBIAN_FRONTEND=noninteractive
ARG QT_VERSION=6.6.3

# System dependencies + CMake from Kitware APT repository (fast mirror, avoids
# slow PyPI wheel downloads that hang on constrained Docker networks).
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    wget \
    gpg \
    && wget -qO - https://apt.kitware.com/keys/kitware-archive-latest.asc \
       | gpg --dearmor -o /usr/share/keyrings/kitware-archive-keyring.gpg \
    && echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ jammy main' \
       > /etc/apt/sources.list.d/kitware.list \
    && apt-get update && apt-get install -y --no-install-recommends \
    # CMake 3.28+ from Kitware repo
    cmake \
    # Build tools
    build-essential \
    gcc-12 \
    g++-12 \
    curl \
    unzip \
    xz-utils \
    # Qt system dependencies (minimal headless set for offscreen + xcb platform)
    libgl1-mesa-dev \
    libgles2-mesa-dev \
    libxcb1-dev \
    libxcb-xinerama0-dev \
    libxcb-xkb-dev \
    libxcb-icccm4-dev \
    libxcb-image0-dev \
    libxcb-keysyms1-dev \
    libxcb-render-util0-dev \
    libxcb-randr0-dev \
    libxcb-shape0-dev \
    libxkbcommon-dev \
    libxkbcommon-x11-dev \
    libfontconfig1-dev \
    libfreetype-dev \
    libdbus-1-dev \
    # Headless display fallback (for any test that requests xcb platform)
    xvfb \
    # Argon2id password hashing
    libargon2-dev \
    # OpenSSL 3.x (AES-256-GCM, Ed25519, HKDF-SHA256)
    libssl-dev \
    # SQLite 3.39+
    libsqlite3-dev \
    sqlite3 \
    # Python3 for aqtinstall
    python3 \
    python3-pip \
    # Build utilities
    git \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

# Set GCC 12 as default compiler
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100

# Install aqtinstall for Qt deployment. CMake is already installed via apt above.
# Qt6::StateMachine is required by desktop/CMakeLists.txt.
# In Qt 6.6.3 aqt packaging, this component is provided by the qtscxml module,
# so install qtscxml explicitly to keep Docker builds deterministic.
RUN pip3 install --no-cache-dir aqtinstall \
    && aqt install-qt linux desktop ${QT_VERSION} gcc_64 \
        -m qtscxml \
        --outputdir /opt/Qt \
    && rm -rf ~/.cache/pip

ENV Qt6_DIR=/opt/Qt/${QT_VERSION}/gcc_64/lib/cmake/Qt6
ENV PATH=/opt/Qt/${QT_VERSION}/gcc_64/bin:$PATH
ENV LD_LIBRARY_PATH=/opt/Qt/${QT_VERSION}/gcc_64/lib

# Coverage tooling: lcov (genhtml + lcov) and gcovr (text + xml reports).
# Installed as a dedicated layer so adding it does not invalidate the large
# Qt install above. Used by the coverage workflow documented in run_tests.sh
# (--coverage flag) — not part of the normal test run.
RUN apt-get update && apt-get install -y --no-install-recommends lcov \
    && rm -rf /var/lib/apt/lists/* \
    && pip3 install --no-cache-dir "gcovr>=6.0,<8.0" \
    && rm -rf ~/.cache/pip

# ── Build stage ─────────────────────────────────────────────────────────────

FROM base AS builder

WORKDIR /workspace
COPY . /workspace

RUN cmake -B build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_C_COMPILER=gcc \
    -DQt6_DIR=${Qt6_DIR} \
    && cmake --build build --parallel "$(nproc)"

# ── Test stage ───────────────────────────────────────────────────────────────

FROM builder AS test-runner

# QT_QPA_PLATFORM=offscreen: Qt does not open any display window.
# Tests that explicitly need xcb are wrapped in xvfb-run automatically.
# CTEST_OUTPUT_ON_FAILURE: failed test output is printed immediately.
ENV QT_QPA_PLATFORM=offscreen
ENV CTEST_OUTPUT_ON_FAILURE=1

WORKDIR /workspace/build

# xvfb-run provides a virtual framebuffer for any test that requests a display.
# All tests are configured with QT_QPA_PLATFORM=offscreen in their CTest
# properties, so xvfb-run is a safety net only.
CMD ["xvfb-run", "--auto-servernum", "--server-args=-screen 0 1280x1024x24", \
     "ctest", "--output-on-failure", "--parallel", "2"]
