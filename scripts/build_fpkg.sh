#!/bin/bash
# =============================================================================
# PS4SplitScreenConnector - FPKG Build Script
# =============================================================================
# Builds FPKG (Fake Package) for jailbroken PS4
# Requires: orbis-pub-fpkg or fpkg tool
# =============================================================================

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build_ps4"
INSTALL_DIR="${PROJECT_ROOT}/install_ps4"
OUTPUT_DIR="${PROJECT_ROOT}/output"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}PS4SplitScreenConnector FPKG Builder${NC}"
echo -e "${BLUE}========================================${NC}"

# -----------------------------------------------------------------------------
# Check/Source PS4SDK
# -----------------------------------------------------------------------------
if [ -z "${PS4SDK:-}" ]; then
    if [ -f ~/openorbis-sdk/environment.sh ]; then
        source ~/openorbis-sdk/environment.sh
    else
        echo -e "${RED}PS4SDK not set! Run: source ~/openorbis-sdk/environment.sh${NC}"
        exit 1
    fi
fi

# -----------------------------------------------------------------------------
# Build Project
# -----------------------------------------------------------------------------
echo -e "${YELLOW}[1/4] Building project...${NC}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake "${PROJECT_ROOT}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="${PS4SDK}/toolchain.cmake" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -G "Unix Makefiles"
make -j"$(nproc)"
make install

# -----------------------------------------------------------------------------
# Verify eboot.bin
# -----------------------------------------------------------------------------
EBOOT="${BUILD_DIR}/bin/splitscreen_demo"
if [ ! -f "${EBOOT}" ]; then
    echo -e "${RED}eboot.bin not found at ${EBOOT}${NC}"
    exit 1
fi

echo -e "${GREEN}Found eboot.bin ($(du -h ${EBOOT} | cut -f1))${NC}"
file "${EBOOT}"

# -----------------------------------------------------------------------------
# Generate FPKG config
# -----------------------------------------------------------------------------
echo -e "${YELLOW}[2/4] Generating FPKG config...${NC}"
mkdir -p "${OUTPUT_DIR}"
cd "${PROJECT_ROOT}"

# Generate unique Content-ID
SHORT_SHA=$(git -C "${PROJECT_ROOT}" rev-parse --short=8 HEAD 2>/dev/null || echo "DEV$(date +%s)")
CONTENT_ID="UP0001-PS4SPLITSCREEN_00-${SHORT_SHA}000000"
PASSCODE="00000000000000000000000000000000"

cp scripts/fpkg.conf.template "${OUTPUT_DIR}/fpkg.conf"
sed -i "s|UP0001-PS4SPLITSCREEN_00-0000000000000000|${CONTENT_ID}|" "${OUTPUT_DIR}/fpkg.conf"
sed -i "s|Passcode = 00000000000000000000000000000000|Passcode = ${PASSCODE}|" "${OUTPUT_DIR}/fpkg.conf"

# Update eboot path
sed -i "s|\"eboot.bin\" = \"build_ps4/bin/splitscreen_demo\"|\"eboot.bin\" = \"${EBOOT}\"|" "${OUTPUT_DIR}/fpkg.conf"

echo "Content-ID: ${CONTENT_ID}"
cat "${OUTPUT_DIR}/fpkg.conf"

# -----------------------------------------------------------------------------
# Create FPKG
# -----------------------------------------------------------------------------
echo -e "${YELLOW}[3/4] Creating FPKG...${NC}"
cd "${OUTPUT_DIR}"

FPKG_TOOL=""
if command -v orbis-pub-fpkg &> /dev/null; then
    FPKG_TOOL="orbis-pub-fpkg"
    echo "Using orbis-pub-fpkg"
elif command -v fpkg &> /dev/null; then
    FPKG_TOOL="fpkg"
    echo "Using fpkg"
else
    echo -e "${YELLOW}No FPKG tool found, building orbis-pub-fpkg...${NC}"
    
    # Build orbis-pub-fpkg from OpenOrbis
    cd ~
    if [ ! -d orbis-pub-gen ]; then
        git clone https://github.com/OpenOrbis/orbis-pub-gen
    fi
    cd orbis-pub-gen
    mkdir -p build && cd build
    cmake .. -DCMAKE_PREFIX_PATH="${PS4SDK}"
    make -j"$(nproc)"
    sudo make install
    
    FPKG_TOOL="orbis-pub-fpkg"
fi

# Generate output filename
FPKG_NAME="PS4SplitScreenConnector_${SHORT_SHA}.fpkg"
FPKG_PATH="${OUTPUT_DIR}/${FPKG_NAME}"

if [ "${FPKG_TOOL}" = "orbis-pub-fpkg" ]; then
    orbis-pub-fpkg -config fpkg.conf -output "${FPKG_PATH}"
elif [ "${FPKG_TOOL}" = "fpkg" ]; then
    fpkg --package fpkg.conf --out "${FPKG_PATH}"
fi

# -----------------------------------------------------------------------------
# Verify
# -----------------------------------------------------------------------------
echo -e "${YELLOW}[4/4] Verifying FPKG...${NC}"
if [ -f "${FPKG_PATH}" ]; then
    SIZE=$(du -h "${FPKG_PATH}" | cut -f1)
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}✅ FPKG CREATED SUCCESSFULLY!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo "File:    ${FPKG_PATH}"
    echo "Size:    ${SIZE}"
    echo "Content-ID: ${CONTENT_ID}"
    echo ""
    echo -e "${BLUE}To install on jailbroken PS4 (9.00 or lower):${NC}"
    echo "  1. Copy ${FPKG_NAME} to USB (exFAT) or FTP to PS4"
    echo "  2. On PS4: Debug Settings → Game → Package Installer"
    echo "  3. Install the .fpkg"
    echo "  4. Run from Homebrew Launcher"
    echo ""
    echo -e "${BLUE}Or use PS4-FPKG-Tool (Windows GUI):${NC}"
    echo "  1. Download PS4-FPKG-Tool from GitHub"
    echo "  2. Open ${FPKG_PATH}"
    echo "  3. Click 'Install' (if PS4 connected via network)"
else
    echo -e "${RED}❌ FPKG creation failed!${NC}"
    exit 1
fi