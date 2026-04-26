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

set(TOOLCHAIN_PREFIX "arm-none-eabi-")
set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_AR           "${TOOLCHAIN_PREFIX}ar")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_PREFIX}objcopy")
set(CMAKE_RANLIB       "${TOOLCHAIN_PREFIX}ranlib")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
