# =============================================================================
# OpenOrbis / PS4SDK Toolchain for CMake
# Place this at $PS4SDK/toolchain.cmake or pass via -DCMAKE_TOOLCHAIN_FILE
# =============================================================================

# -----------------------------------------------------------------------------
# Compiler Configuration (Clang targeting FreeBSD/Orbis x86_64)
# -----------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSTEM_VERSION 1.0)

# Toolchain prefix (typically: x86_64-scei-ps4-elf- or orbis-)
if(NOT DEFINED ORBIS_TOOLCHAIN_PREFIX)
    set(ORBIS_TOOLCHAIN_PREFIX "x86_64-scei-ps4-elf-")
endif()

set(CMAKE_C_COMPILER   "${ORBIS_TOOLCHAIN_PREFIX}clang")
set(CMAKE_CXX_COMPILER "${ORBIS_TOOLCHAIN_PREFIX}clang++")
set(CMAKE_ASM_COMPILER "${ORBIS_TOOLCHAIN_PREFIX}clang")
set(CMAKE_AR           "${ORBIS_TOOLCHAIN_PREFIX}ar")
set(CMAKE_RANLIB       "${ORBIS_TOOLCHAIN_PREFIX}ranlib")
set(CMAKE_OBJCOPY      "${ORBIS_TOOLCHAIN_PREFIX}objcopy")
set(CMAKE_STRIP        "${ORBIS_TOOLCHAIN_PREFIX}strip")
set(CMAKE_OBJDUMP      "${ORBIS_TOOLCHAIN_PREFIX}objdump")

# -----------------------------------------------------------------------------
# SDK Paths
# -----------------------------------------------------------------------------
if(DEFINED ENV{PS4SDK})
    set(PS4SDK_ROOT $ENV{PS4SDK})
else()
    message(FATAL_ERROR "PS4SDK environment variable not set. Source the OpenOrbis environment script first.")
endif()

set(ORBIS_SDK_ROOT "${PS4SDK_ROOT}")
set(ORBIS_INCLUDE_DIR "${ORBIS_SDK_ROOT}/include")
set(ORBIS_LIB_DIR "${ORBIS_SDK_ROOT}/lib")

# -----------------------------------------------------------------------------
# Compiler Flags
# -----------------------------------------------------------------------------
set(ORBIS_BASE_FLAGS
    "-target x86_64-scei-ps4-elf"
    "-fPIC"
    "-fno-exceptions"
    "-fno-rtti"
    "-fno-stack-protector"
    "-fno-unwind-tables"
    "-fno-asynchronous-unwind-tables"
    "-mno-sse4a"
    "-mno-avx"
    "-mno-avx2"
    "-mno-fma"
    "-m64"
    "-pipe"
)

set(ORBIS_C_FLAGS "${ORBIS_BASE_FLAGS} -std=gnu11")
set(ORBIS_CXX_FLAGS "${ORBIS_BASE_FLAGS} -std=gnu++17")
set(ORBIS_ASM_FLAGS "${ORBIS_BASE_FLAGS}")

set(CMAKE_C_FLAGS   "${ORBIS_C_FLAGS}"   CACHE STRING "C flags for PS4")
set(CMAKE_CXX_FLAGS "${ORBIS_CXX_FLAGS}" CACHE STRING "C++ flags for PS4")
set(CMAKE_ASM_FLAGS "${ORBIS_ASM_FLAGS}" CACHE STRING "ASM flags for PS4")

# -----------------------------------------------------------------------------
# Linker Flags
# -----------------------------------------------------------------------------
set(ORBIS_LINK_FLAGS
    "-target x86_64-scei-ps4-elf"
    "-Wl,--gc-sections"
    "-Wl,--as-needed"
    "-Wl,-z,noexecstack"
    "-Wl,-z,relro"
    "-Wl,-z,now"
    "-nostdlib"
    "-L${ORBIS_LIB_DIR}"
)

set(CMAKE_EXE_LINKER_FLAGS "${ORBIS_LINK_FLAGS}" CACHE STRING "Exe linker flags")
set(CMAKE_SHARED_LINKER_FLAGS "${ORBIS_LINK_FLAGS}" CACHE STRING "Shared linker flags")
set(CMAKE_MODULE_LINKER_FLAGS "${ORBIS_LINK_FLAGS}" CACHE STRING "Module linker flags")

# -----------------------------------------------------------------------------
# Sysroot and Include Paths
# -----------------------------------------------------------------------------
set(CMAKE_SYSROOT "${ORBIS_SDK_ROOT}")
set(CMAKE_FIND_ROOT_PATH "${ORBIS_SDK_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# -----------------------------------------------------------------------------
# System Includes
# -----------------------------------------------------------------------------
set(CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES
    "${ORBIS_INCLUDE_DIR}"
    "${ORBIS_INCLUDE_DIR}/ps4"
)
set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES
    "${ORBIS_INCLUDE_DIR}"
    "${ORBIS_INCLUDE_DIR}/ps4"
)

# -----------------------------------------------------------------------------
# PS4 SDK Stubs (auto-linked by toolchain)
# -----------------------------------------------------------------------------
# These are provided as .stub libraries by the SDK
set(PS4_STUB_LIBS
    SceSysmodule_stub
    SceLibKernel_stub
    SceLibc_stub
    ScePthread_stub
    SceNet_stub
    SceNetCtl_stub
    SceGnm_stub
    SceGnmDriver_stub
    SceVideoOut_stub
    ScePad_stub
    SceAudioOut_stub
    SceSystemService_stub
    SceUserService_stub
    SceJpeg_stub
    ScePng_stub
)

# -----------------------------------------------------------------------------
# Helper function to create PS4 ELF/SELF
# -----------------------------------------------------------------------------
function(orbis_create_self target output_name)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> ${output_name}.elf
        COMMAND ${ORBIS_SDK_ROOT}/bin/orbis-elf-create-self ${output_name}.elf ${output_name}.self
        COMMENT "Creating PS4 SELF: ${output_name}.self"
    )
endfunction()

# -----------------------------------------------------------------------------
# Helper function to create PS4 PKG (requires orbis-pub-gen)
# -----------------------------------------------------------------------------
function(orbis_create_pkg target pkg_name title_id content_id)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/pkg_temp/sce_sys
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${CMAKE_BINARY_DIR}/pkg_temp/eboot.bin
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/param.sfo ${CMAKE_BINARY_DIR}/pkg_temp/sce_sys/param.sfo
        COMMAND ${ORBIS_SDK_ROOT}/bin/orbis-pub-gen
            -config ${CMAKE_SOURCE_DIR}/package.conf
            -output ${CMAKE_BINARY_DIR}/${pkg_name}.pkg
        COMMENT "Creating PS4 PKG: ${pkg_name}.pkg"
    )
endfunction()

message(STATUS "OpenOrbis toolchain loaded:")
message(STATUS "  CC:  ${CMAKE_C_COMPILER}")
message(STATUS "  CXX: ${CMAKE_CXX_COMPILER}")
message(STATUS "  SDK: ${ORBIS_SDK_ROOT}")