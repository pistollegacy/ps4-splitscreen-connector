cmake_minimum_required(VERSION 3.16)
project(PS4SplitScreenConnector VERSION 1.0.0 LANGUAGES C CXX ASM)

# =============================================================================
# PS4SplitScreenConnector - LAN Splitscreen Framework for PS4 Homebrew
# Built for OpenOrbis / PS4SDK
# =============================================================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# -----------------------------------------------------------------------------
# OpenOrbis / PS4SDK Toolchain Detection
# -----------------------------------------------------------------------------
# Expects PS4SDK environment variable or CMAKE_TOOLCHAIN_FILE pointing to
# the OpenOrbis toolchain (typically: $PS4SDK/toolchain.cmake)

if(DEFINED ENV{PS4SDK})
    set(PS4SDK_PATH $ENV{PS4SDK})
    message(STATUS "Found PS4SDK at: ${PS4SDK_PATH}")
elseif(NOT CMAKE_TOOLCHAIN_FILE)
    message(WARNING "PS4SDK not found in environment. Set PS4SDK env var or pass -DCMAKE_TOOLCHAIN_FILE=<path>")
endif()

# -----------------------------------------------------------------------------
# Compiler Flags for PS4 (Orbis/FreeBSD-based)
# -----------------------------------------------------------------------------
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -fno-exceptions
    -fno-rtti
    -ffunction-sections
    -fdata-sections
)

add_link_options(
    -Wl,--gc-sections
    -Wl,--as-needed
)

# PS4-specific defines
add_compile_definitions(
    __ORBIS__
    __GNUC__
    _GNU_SOURCE
    _POSIX_C_SOURCE=200809L
    PS4SPLITSCREEN_EXPORTS
)

# -----------------------------------------------------------------------------
# Include Directories
# -----------------------------------------------------------------------------
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

# -----------------------------------------------------------------------------
# Find PS4 System Libraries (provided by PS4SDK)
# -----------------------------------------------------------------------------
# These are linked automatically by the toolchain, but we declare them for clarity
set(PS4_SYSTEM_LIBS
    SceSysmodule_stub
    SceNet_stub
    SceNetCtl_stub
    SceLibc_stub
    SceLibKernel_stub
    ScePthread_stub
    SceGnm_stub
    SceGnmDriver_stub
    SceVideoOut_stub
    ScePad_stub
    SceAudioOut_stub
    SceSystemService_stub
    SceUserService_stub
)

# -----------------------------------------------------------------------------
# Library: ps4splitscreen (static library)
# -----------------------------------------------------------------------------
set(SPLITSCREEN_SOURCES
    src/network/ps4_net_init.c
    src/network/ps4_socket.c
    src/network/ps4_discovery.c
    src/network/reliability_layer.c
    src/network/packet_serialization.c
    src/sync/lockstep_sync.c
    src/sync/rollback_netcode.c
    src/sync/state_buffer.c
    src/sync/deterministic_rng.c
    src/render/splitscreen_viewport.c
    src/render/ps4_framebuffer.c
    src/render/ps4_gnm_helpers.c
    src/core/session_manager.c
    src/core/input_sync.c
    src/core/time_sync.c
    src/core/logger.c
)

add_library(ps4splitscreen STATIC ${SPLITSCREEN_SOURCES})
target_link_libraries(ps4splitscreen PRIVATE ${PS4_SYSTEM_LIBS})
target_include_directories(ps4splitscreen PUBLIC ${CMAKE_SOURCE_DIR}/include)

# -----------------------------------------------------------------------------
# Example: Basic Splitscreen Demo
# -----------------------------------------------------------------------------
add_executable(splitscreen_demo
    examples/splitscreen_demo/main.c
    examples/splitscreen_demo/game_logic.c
    examples/splitscreen_demo/input_handler.c
    examples/splitscreen_demo/render_game.c
)
target_link_libraries(splitscreen_demo PRIVATE ps4splitscreen ${PS4_SYSTEM_LIBS})

# -----------------------------------------------------------------------------
# Example: Network Stress Test
# -----------------------------------------------------------------------------
add_executable(net_stress_test
    examples/net_stress_test/main.c
    examples/net_stress_test/stress_client.c
    examples/net_stress_test/stress_server.c
)
target_link_libraries(net_stress_test PRIVATE ps4splitscreen ${PS4_SYSTEM_LIBS})

# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------
install(TARGETS ps4splitscreen
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ps4splitscreen/
    DESTINATION include/ps4splitscreen
    FILES_MATCHING PATTERN "*.h"
)

# -----------------------------------------------------------------------------
# CPack for .pkg generation (requires orbis-pub-gen)
# -----------------------------------------------------------------------------
if(DEFINED ENV{ORBIS_PUB_GEN})
    set(CPACK_GENERATOR "PS4")
    set(CPACK_PACKAGE_NAME "PS4SplitScreenConnector")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_DESCRIPTION "LAN Splitscreen Framework for PS4 Homebrew")
    include(CPack)
endif()

# -----------------------------------------------------------------------------
# Custom Targets
# -----------------------------------------------------------------------------
add_custom_target(ps4-package
    COMMAND ${CMAKE_COMMAND} -E echo "Package with: orbis-pub-gen -config package.conf"
    COMMENT "Instructions for creating PS4 .pkg"
)

# Print build info
message(STATUS "========================================")
message(STATUS "PS4SplitScreenConnector Build Config")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  PS4SDK: ${PS4SDK_PATH}")
message(STATUS "========================================")