# CMake toolchain file for the OpSy CI build.
#
# Selects the cross-compilation toolchain (arm-none-eabi-gcc) without
# pinning a specific CPU. The actual @c -mcpu / @c -mfpu flags are added
# by the downstream @c CMakeLists.txt based on @c OPSY_TARGET so the same
# toolchain file works for every Cortex-M variant in the CI matrix.
#
# Assumes @c arm-none-eabi-gcc and friends are on @c PATH (the CI installs
# them via @c carlosperate/arm-none-eabi-gcc-action).

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  arm)

# Don't try to link a final executable when CMake probes the compiler;
# the OpSy sanity build produces a static library on purpose.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Pick the compiler family from -DOPSY_COMPILER=gcc|clang (default: gcc).
# Both expect the corresponding toolchain to be on PATH; the CI installs
# them via carlosperate/arm-none-eabi-gcc-action (gcc) or by extracting
# the LLVM Embedded Toolchain for Arm release tarball (clang).
if(NOT DEFINED OPSY_COMPILER)
    set(OPSY_COMPILER "gcc" CACHE STRING "Toolchain family: gcc or clang")
endif()

if(OPSY_COMPILER STREQUAL "gcc")
    set(TOOLCHAIN_PREFIX "arm-none-eabi-")
    set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
    set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc")
    set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
    set(CMAKE_AR           "${TOOLCHAIN_PREFIX}ar")
    set(CMAKE_OBJCOPY      "${TOOLCHAIN_PREFIX}objcopy")
    set(CMAKE_RANLIB       "${TOOLCHAIN_PREFIX}ranlib")
elseif(OPSY_COMPILER STREQUAL "clang")
    set(CMAKE_C_COMPILER   "clang")
    set(CMAKE_ASM_COMPILER "clang")
    set(CMAKE_CXX_COMPILER "clang++")
    set(CMAKE_AR           "llvm-ar")
    set(CMAKE_OBJCOPY      "llvm-objcopy")
    set(CMAKE_RANLIB       "llvm-ranlib")
    # Bare-metal ARM target. Per-CPU flags (-mcpu/-mfpu/-mfloat-abi) come
    # from the downstream CMakeLists.txt via OPSY_TARGET.
    set(CMAKE_C_FLAGS_INIT   "--target=arm-none-eabi")
    set(CMAKE_CXX_FLAGS_INIT "--target=arm-none-eabi")
    set(CMAKE_ASM_FLAGS_INIT "--target=arm-none-eabi")
else()
    message(FATAL_ERROR "Unknown OPSY_COMPILER: ${OPSY_COMPILER} (expected gcc or clang)")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
