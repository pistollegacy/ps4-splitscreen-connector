#!/bin/bash
# =============================================================================
# PS4SplitScreenConnector Build Script for PS4 (OpenOrbis)
# =============================================================================
# Usage: ./scripts/build_ps4.sh [Debug|Release] [clean]
# 
# Requires: OpenOrbis SDK sourced (source /path/to/openorbis/environment.sh)
# =============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
BUILD_TYPE="${1:-Release}"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build_ps4"
INSTALL_DIR="${PROJECT_ROOT}/install_ps4"

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}PS4SplitScreenConnector Build${NC}"
echo -e "${GREEN}========================================${NC}"
echo "Project:  ${PROJECT_ROOT}"
echo "Build:    ${BUILD_DIR}"
echo "Install:  ${INSTALL_DIR}"
echo "Type:     ${BUILD_TYPE}"
echo ""

# Check for OpenOrbis environment
if [ -z "$PS4SDK" ]; then
    echo -e "${RED}ERROR: PS4SDK environment variable not set!${NC}"
    echo "Source the OpenOrbis environment first:"
    echo "  source /path/to/openorbis/environment.sh"
    exit 1
fi

echo -e "${GREEN}Using PS4SDK: ${PS4SDK}${NC}"

# Clean if requested
if [ "$2" = "clean" ] || [ "$1" = "clean" ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "${BUILD_DIR}"
    rm -rf "${INSTALL_DIR}"
fi

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure with CMake
echo -e "${GREEN}Configuring with CMake...${NC}"
cmake "${PROJECT_ROOT}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_TOOLCHAIN_FILE="${PS4SDK}/toolchain.cmake" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -G "Unix Makefiles"

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
fi

# Build
echo -e "${GREEN}Building...${NC}"
CPU_COUNT=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
make -j${CPU_COUNT}

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

# Install
echo -e "${GREEN}Installing...${NC}"
make install

if [ $? -ne 0 ]; then
    echo -e "${RED}Install failed!${NC}"
    exit 1
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Build successful!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Installed to: ${INSTALL_DIR}"
echo ""
echo "Library: ${INSTALL_DIR}/lib/libps4splitscreen.a"
echo "Headers: ${INSTALL_DIR}/include/ps4splitscreen/"
echo ""
echo "To create a .pkg for PS4:"
echo "  1. Create package.conf (see scripts/package.conf.template)"
echo "  2. Run: orbis-pub-gen -config package.conf -output game.pkg"
echo ""

# List built targets
echo "Built targets:"
ls -la "${BUILD_DIR}/bin/" 2>/dev/null || true
ls -la "${BUILD_DIR}/lib/" 2>/dev/null || true