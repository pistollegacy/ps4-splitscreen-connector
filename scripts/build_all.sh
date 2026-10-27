#!/bin/bash
# =============================================================================
# PS4SplitScreenConnector - One-Command Build Script
# =============================================================================
# Usage: ./scripts/build_all.sh [clean|docker|github|fpkg|docker-fpkg|github-fpkg]
# 
# Modes:
#   (default)      - Build locally (.pkg for official SDK)
#   clean          - Full clean rebuild (.pkg)
#   docker         - Build using Docker (.pkg)
#   github         - Push to GitHub and trigger Actions build (.pkg)
#   fpkg           - Build locally (.fpkg for jailbroken PS4) ⭐
#   docker-fpkg    - Build using Docker (.fpkg for jailbroken PS4)
#   github-fpkg    - Push to GitHub and trigger Actions build (.fpkg)
#   setup          - Setup OpenOrbis toolchain only
# =============================================================================

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build_ps4"
INSTALL_DIR="${PROJECT_ROOT}/install_ps4"
OUTPUT_DIR="${PROJECT_ROOT}/output"

MODE="${1:-local}"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}PS4SplitScreenConnector Build System${NC}"
echo -e "${BLUE}========================================${NC}"
echo "Mode:    ${MODE}"
echo "Project: ${PROJECT_ROOT}"
echo ""

# -----------------------------------------------------------------------------
# Helper Functions
# -----------------------------------------------------------------------------
generate_content_id() {
    local short_sha
    short_sha=$(git -C "${PROJECT_ROOT}" rev-parse --short=8 HEAD 2>/dev/null || echo "DEV$(date +%s)")
    echo "UP0001-PS4SPLITSCREEN_00-${short_sha}000000"
}

prepare_package_conf() {
    local content_id="$1"
    cd "${PROJECT_ROOT}"
    cp scripts/package.conf.template package.conf
    sed -i "s|UP0000-PS4SPLITSCREEN_00-ABCDEF1234567890|${content_id}|" package.conf
    echo -e "${GREEN}Generated package.conf with Content-ID: ${content_id}${NC}"
}

# -----------------------------------------------------------------------------
# Local Build (WSL2/Linux)
# -----------------------------------------------------------------------------
build_local() {
    echo -e "${YELLOW}[1/5] Checking PS4SDK...${NC}"
    if [ -z "${PS4SDK:-}" ]; then
        if [ -f ~/openorbis-sdk/environment.sh ]; then
            echo "Sourcing ~/openorbis-sdk/environment.sh"
            source ~/openorbis-sdk/environment.sh
        else
            echo -e "${RED}PS4SDK not set! Run: source ~/openorbis-sdk/environment.sh${NC}"
            echo "Or build toolchain first: ./scripts/build_all.sh setup"
            exit 1
        fi
    fi
    echo "PS4SDK = ${PS4SDK}"

    echo -e "${YELLOW}[2/5] Configuring CMake...${NC}"
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    cmake "${PROJECT_ROOT}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="${PS4SDK}/toolchain.cmake" \
        -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
        -G "Unix Makefiles"

    echo -e "${YELLOW}[3/5] Building...${NC}"
    make -j"$(nproc)"

    echo -e "${YELLOW}[4/5] Installing...${NC}"
    make install

    echo -e "${YELLOW}[5/5] Creating .pkg...${NC}"
    mkdir -p "${OUTPUT_DIR}"
    cd "${PROJECT_ROOT}"
    
    CONTENT_ID=$(generate_content_id)
    prepare_package_conf "${CONTENT_ID}"

    # Try orbis-pub-gen
    if command -v orbis-pub-gen &> /dev/null; then
        orbis-pub-gen -config package.conf -output "${OUTPUT_DIR}/PS4SplitScreenConnector.pkg"
    else
        echo -e "${YELLOW}orbis-pub-gen not found, attempting to build...${NC}"
        build_orbis_pub_gen
        orbis-pub-gen -config package.conf -output "${OUTPUT_DIR}/PS4SplitScreenConnector.pkg"
    fi

    verify_pkg "${OUTPUT_DIR}/PS4SplitScreenConnector.pkg"
}

build_orbis_pub_gen() {
    cd ~
    if [ ! -d orbis-pub-gen ]; then
        git clone https://github.com/OpenOrbis/orbis-pub-gen
    fi
    cd orbis-pub-gen
    mkdir -p build && cd build
    cmake .. -DCMAKE_PREFIX_PATH="${PS4SDK}"
    make -j"$(nproc)"
    sudo make install
}

verify_pkg() {
    local pkg_path="$1"
    if [ -f "${pkg_path}" ]; then
        local size
        size=$(du -h "${pkg_path}" | cut -f1)
        echo -e "${GREEN}========================================${NC}"
        echo -e "${GREEN}✅ BUILD SUCCESSFUL!${NC}"
        echo -e "${GREEN}========================================${NC}"
        echo "Package: ${pkg_path}"
        echo "Size:    ${size}"
        echo ""
        echo "To install on PS4:"
        echo "  1. Copy ${pkg_path} to USB (exFAT) or FTP to PS4"
        echo "  2. On PS4: Debug Settings → Game → Package Installer"
        echo "  3. Run from Homebrew Launcher"
    else
        echo -e "${RED}❌ Package not created!${NC}"
        exit 1
    fi
}

# -----------------------------------------------------------------------------
# FPKG Build (for jailbroken PS4)
# -----------------------------------------------------------------------------
build_fpkg_local() {
    echo -e "${YELLOW}[1/4] Building project...${NC}"
    if [ -z "${PS4SDK:-}" ]; then
        if [ -f ~/openorbis-sdk/environment.sh ]; then
            source ~/openorbis-sdk/environment.sh
        else
            echo -e "${RED}PS4SDK not set! Run: source ~/openorbis-sdk/environment.sh${NC}"
            exit 1
        fi
    fi

    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    cmake "${PROJECT_ROOT}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="${PS4SDK}/toolchain.cmake" \
        -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
        -G "Unix Makefiles"
    make -j"$(nproc)"
    make install

    EBOOT="${BUILD_DIR}/bin/splitscreen_demo"
    if [ ! -f "${EBOOT}" ]; then
        echo -e "${RED}eboot.bin not found!${NC}"
        exit 1
    fi

    echo -e "${YELLOW}[2/4] Generating FPKG config...${NC}"
    mkdir -p "${OUTPUT_DIR}"
    cd "${OUTPUT_DIR}"

    SHORT_SHA=$(git -C "${PROJECT_ROOT}" rev-parse --short=8 HEAD 2>/dev/null || echo "DEV$(date +%s)")
    CONTENT_ID="UP0001-PS4SPLITSCREEN_00-${SHORT_SHA}000000"
    PASSCODE="00000000000000000000000000000000"

    cp "${PROJECT_ROOT}/scripts/fpkg.conf.template" fpkg.conf
    sed -i "s|UP0001-PS4SPLITSCREEN_00-0000000000000000|${CONTENT_ID}|" fpkg.conf
    sed -i "s|Passcode = 00000000000000000000000000000000|Passcode = ${PASSCODE}|" fpkg.conf
    sed -i "s|\"eboot.bin\" = \"build_ps4/bin/splitscreen_demo\"|\"eboot.bin\" = \"${EBOOT}\"|" fpkg.conf

    echo "Content-ID: ${CONTENT_ID}"

    echo -e "${YELLOW}[3/4] Creating FPKG...${NC}"
    
    # Ensure orbis-pub-fpkg is available
    if ! command -v orbis-pub-fpkg &> /dev/null; then
        echo "Building orbis-pub-fpkg..."
        cd ~
        if [ ! -d orbis-pub-gen ]; then
            git clone https://github.com/OpenOrbis/orbis-pub-gen
        fi
        cd orbis-pub-gen
        mkdir -p build && cd build
        cmake .. -DCMAKE_PREFIX_PATH="${PS4SDK}"
        make -j"$(nproc)"
        sudo make install
    fi

    FPKG_NAME="PS4SplitScreenConnector_${SHORT_SHA}.fpkg"
    orbis-pub-fpkg -config fpkg.conf -output "${FPKG_NAME}"

    echo -e "${YELLOW}[4/4] Verifying FPKG...${NC}"
    verify_fpkg "${OUTPUT_DIR}/${FPKG_NAME}"
}

verify_fpkg() {
    local fpkg_path="$1"
    if [ -f "${fpkg_path}" ]; then
        local size
        size=$(du -h "${fpkg_path}" | cut -f1)
        echo -e "${GREEN}========================================${NC}"
        echo -e "${GREEN}✅ FPKG CREATED SUCCESSFULLY!${NC}"
        echo -e "${GREEN}========================================${NC}"
        echo "File:    ${fpkg_path}"
        echo "Size:    ${size}"
        echo ""
        echo -e "${BLUE}To install on jailbroken PS4 (9.00 or lower):${NC}"
        echo "  1. Copy $(basename ${fpkg_path}) to USB (exFAT) or FTP to PS4"
        echo "  2. On PS4: Debug Settings → Game → Package Installer"
        echo "  3. Install the .fpkg"
        echo "  4. Run from Homebrew Launcher"
    else
        echo -e "${RED}❌ FPKG not created!${NC}"
        exit 1
    fi
}

# -----------------------------------------------------------------------------
# Docker FPKG Build
# -----------------------------------------------------------------------------
build_docker_fpkg() {
    echo -e "${YELLOW}Building FPKG with Docker...${NC}"
    
    if ! command -v docker &> /dev/null; then
        echo -e "${RED}Docker not installed!${NC}"
        exit 1
    fi

    mkdir -p "${OUTPUT_DIR}"
    
    SHORT_SHA=$(git -C "${PROJECT_ROOT}" rev-parse --short=8 HEAD 2>/dev/null || echo "DEV$(date +%s)")
    CONTENT_ID="UP0001-PS4SPLITSCREEN_00-${SHORT_SHA}000000"
    PASSCODE="00000000000000000000000000000000"

    DOCKER_BUILDKIT=1 docker build \
        -f "${PROJECT_ROOT}/Dockerfile.fpkg" \
        --build-arg CONTENT_ID="${CONTENT_ID}" \
        --build-arg PASSCODE="${PASSCODE}" \
        -t ps4-fpkg-builder \
        "${PROJECT_ROOT}"

    echo "Extracting FPKG..."
    docker create --name temp-fpkg-extract ps4-fpkg-builder
    docker cp temp-fpkg-extract:/PS4SplitScreenConnector.fpkg "${OUTPUT_DIR}/PS4SplitScreenConnector_${SHORT_SHA}.fpkg"
    docker rm temp-fpkg-extract

    verify_fpkg "${OUTPUT_DIR}/PS4SplitScreenConnector_${SHORT_SHA}.fpkg"
}

# -----------------------------------------------------------------------------
# GitHub Actions FPKG Trigger
# -----------------------------------------------------------------------------
build_github_fpkg() {
    echo -e "${YELLOW}Triggering GitHub Actions FPKG build...${NC}"
    
    if ! command -v gh &> /dev/null; then
        echo -e "${RED}GitHub CLI (gh) not installed!${NC}"
        echo "Install: https://cli.github.com/"
        exit 1
    fi

    cd "${PROJECT_ROOT}"
    
    if ! git diff --quiet || ! git diff --cached --quiet; then
        git add .
        git commit -m "FPKG build trigger: $(date -u +%Y%m%d-%H%M%S)"
    fi
    
    git push origin main
    
    echo "Triggering FPKG workflow..."
    gh workflow run build_fpkg.yml
    
    echo -e "${GREEN}FPKG build triggered! Check:${NC}"
    echo "  https://github.com/$(gh repo view --json owner,name -q '.owner.login + \"/\" + .name')/actions"
    echo ""
    echo "Download .fpkg from Actions artifacts when complete (~10-20 min)"
}

# -----------------------------------------------------------------------------
# Docker Build
# -----------------------------------------------------------------------------
build_docker() {
    echo -e "${YELLOW}Building with Docker...${NC}"
    
    if ! command -v docker &> /dev/null; then
        echo -e "${RED}Docker not installed!${NC}"
        exit 1
    fi

    mkdir -p "${OUTPUT_DIR}"
    
    CONTENT_ID=$(generate_content_id)
    prepare_package_conf "${CONTENT_ID}"

    # Build with BuildKit for better caching
    DOCKER_BUILDKIT=1 docker build \
        --target pkg-builder \
        --build-arg CONTENT_ID="${CONTENT_ID}" \
        -t ps4-splitscreen-builder \
        "${PROJECT_ROOT}"

    # Extract .pkg from image
    echo "Extracting .pkg..."
    docker create --name temp-extract ps4-splitscreen-builder
    docker cp temp-extract:/PS4SplitScreenConnector.pkg "${OUTPUT_DIR}/PS4SplitScreenConnector.pkg"
    docker rm temp-extract

    verify_pkg "${OUTPUT_DIR}/PS4SplitScreenConnector.pkg"
}

# -----------------------------------------------------------------------------
# GitHub Actions Trigger
# -----------------------------------------------------------------------------
build_github() {
    echo -e "${YELLOW}Triggering GitHub Actions build...${NC}"
    
    if ! command -v gh &> /dev/null; then
        echo -e "${RED}GitHub CLI (gh) not installed!${NC}"
        echo "Install: https://cli.github.com/"
        exit 1
    fi

    cd "${PROJECT_ROOT}"
    
    # Commit and push if needed
    if ! git diff --quiet || ! git diff --cached --quiet; then
        git add .
        git commit -m "Build trigger: $(date -u +%Y%m%d-%H%M%S)"
    fi
    
    git push origin main
    
    echo "Triggering workflow..."
    gh workflow run build_ps4.yml
    
    echo -e "${GREEN}Build triggered! Check:${NC}"
    echo "  https://github.com/$(gh repo view --json owner,name -q '.owner.login + \"/\" + .name')/actions"
    echo ""
    echo "Download .pkg from Actions artifacts when complete (~10-20 min)"
}

# -----------------------------------------------------------------------------
# Setup Toolchain
# -----------------------------------------------------------------------------
setup_toolchain() {
    echo -e "${YELLOW}Setting up OpenOrbis Toolchain...${NC}"
    cd ~
    
    if [ -d openorbis-sdk ]; then
        echo "openorbis-sdk already exists, updating..."
        cd openorbis-sdk
        git pull --recurse-submodules
    else
        git clone --recursive https://github.com/OpenOrbis/openorbis-sdk
        cd openorbis-sdk
    fi
    
    echo "Building toolchain (30-60 min)..."
    ./build-toolchain.sh 2>&1 | tee build-toolchain.log
    
    echo -e "${GREEN}Toolchain built! Add to ~/.bashrc:${NC}"
    echo "  source ~/openorbis-sdk/environment.sh"
}

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------
case "${MODE}" in
    clean)
        echo -e "${YELLOW}Cleaning...${NC}"
        rm -rf "${BUILD_DIR}" "${INSTALL_DIR}" "${OUTPUT_DIR}" package.conf
        build_local
        ;;
    docker)
        build_docker
        ;;
    github)
        build_github
        ;;
    fpkg)
        build_fpkg_local
        ;;
    docker-fpkg)
        build_docker_fpkg
        ;;
    github-fpkg)
        build_github_fpkg
        ;;
    setup)
        setup_toolchain
        ;;
    local|*)
        build_local
        ;;
esac