# On-target OpSy test suite, run under QEMU.
#
# The cross build one directory up proves the code compiles; the host suite
# proves the portable half behaves. Neither runs the scheduler. This one boots
# a real image on an emulated Cortex-M, with SysTick firing and context
# switches happening, which is the only way to exercise task termination,
# condition variable timeouts and priority changes.
#
#   cmake -S tests/qemu -B build-qemu -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE="$PWD/tests/cortex-m-toolchain.cmake" \
#         -DOPSY_COMPILER=gcc -DOPSY_TARGET=m4
#   cmake --build build-qemu
#   ctest --test-dir build-qemu --output-on-failure
#
# The ctest step needs qemu-system-arm on PATH.

cmake_minimum_required(VERSION 3.20)
project(opsy_qemu_tests LANGUAGES CXX ASM)

include(CTest)

if(NOT DEFINED OPSY_TARGET)
    set(OPSY_TARGET "m4" CACHE STRING "Cortex-M target: m3, m4, m7, m33")
endif()

# Per target: compiler flags, QEMU machine, and which linker script applies.
# mps2-an505 is ARMv8-M and boots Secure, where code lives at 0x10000000 and
# SRAM at 0x30000000 — the ARMv7-M addresses are the Non-secure aliases and
# lock the core up on entry.
if(OPSY_TARGET STREQUAL "m3")
    set(OPSY_CPU_FLAGS -mcpu=cortex-m3 -mthumb -mfloat-abi=soft)
    set(OPSY_QEMU_MACHINE mps2-an385)
    set(OPSY_LINKER_SCRIPT armv7m.ld)
elseif(OPSY_TARGET STREQUAL "m4")
    set(OPSY_CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
    set(OPSY_QEMU_MACHINE mps2-an386)
    set(OPSY_LINKER_SCRIPT armv7m.ld)
elseif(OPSY_TARGET STREQUAL "m7")
    set(OPSY_CPU_FLAGS -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)
    set(OPSY_QEMU_MACHINE mps2-an500)
    set(OPSY_LINKER_SCRIPT armv7m.ld)
elseif(OPSY_TARGET STREQUAL "m33")
    set(OPSY_CPU_FLAGS -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard)
    set(OPSY_QEMU_MACHINE mps2-an505)
    set(OPSY_LINKER_SCRIPT armv8m.ld)
else()
    message(FATAL_ERROR "Unknown OPSY_TARGET: ${OPSY_TARGET} (expected m3, m4, m7 or m33)")
endif()

set(OPSY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(OPSY_LINKER_SCRIPT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/${OPSY_LINKER_SCRIPT})

add_executable(opsy_qemu_tests
    ${OPSY_ROOT}/scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/startup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/qemu_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_condition_variable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_critical_section.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_isr_lock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_mutex.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_task.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_time.cpp
)

set_target_properties(opsy_qemu_tests PROPERTIES SUFFIX ".elf")

target_include_directories(opsy_qemu_tests PRIVATE ${OPSY_ROOT} ${CMAKE_CURRENT_SOURCE_DIR})

# OpSy targets C++23, and is kept building under C++26 as that lands: the CI
# builds both so a construct C++26 changes or removes is caught here rather
# than by a user on a newer toolchain. Override with -DOPSY_CXX_STANDARD=26.
if(NOT DEFINED OPSY_CXX_STANDARD)
    set(OPSY_CXX_STANDARD 23 CACHE STRING "C++ standard to build with: 23 or 26")
endif()

set_target_properties(opsy_qemu_tests PROPERTIES
    CXX_STANDARD ${OPSY_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Same strict set as the other two suites.
target_compile_options(opsy_qemu_tests PRIVATE
    ${OPSY_CPU_FLAGS}
    -Wall -Werror -Wpedantic -Wextra
    -Wshadow -Wcast-align -Wcast-qual -Wnull-dereference
    -Wconversion -Wsign-conversion -Wdouble-promotion
    -Wno-psabi
    -fno-exceptions -fno-rtti
    -ffunction-sections -fdata-sections
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(opsy_qemu_tests PRIVATE
        -Wlogical-op
        -Wduplicated-cond
        -Wduplicated-branches
    )
endif()

target_link_options(opsy_qemu_tests PRIVATE
    ${OPSY_CPU_FLAGS}
    -T${OPSY_LINKER_SCRIPT_PATH}
    -nostartfiles
    -Wl,--gc-sections
    # The image is one RWX load segment by design: code, .data's load image
    # and the vector table share the board's ZBT SRAM, and there is no MPU or
    # loader here to care. Binutils 2.39+ warns about it on every link.
    -Wl,--no-warn-rwx-segments
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/opsy_qemu_tests.map
)

# Relink when the linker script changes.
set_target_properties(opsy_qemu_tests PROPERTIES LINK_DEPENDS ${OPSY_LINKER_SCRIPT_PATH})

# The runner turns QEMU's output into a verdict. QEMU maps every non-zero
# application exit code onto a process status of 1, and a hung image would
# otherwise look like a pass, so the script insists on seeing the RESULT line.
add_test(
    NAME opsy_qemu_tests_${OPSY_TARGET}
    COMMAND ${CMAKE_COMMAND}
            -DQEMU_MACHINE=${OPSY_QEMU_MACHINE}
            -DIMAGE=$<TARGET_FILE:opsy_qemu_tests>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_qemu.cmake
)

set_tests_properties(opsy_qemu_tests_${OPSY_TARGET} PROPERTIES TIMEOUT 120)

# OpSy never allocates, so an image built on it should contain no allocator.
# Checked on the linked image because that is where one appears: a single
# construct reaching the global operator new or delete is enough for the
# linker to pull in malloc, free and the _sbrk under them, and nothing about
# it fails to build. See check_no_heap.cmake.
get_filename_component(OPSY_TOOLCHAIN_BIN ${CMAKE_CXX_COMPILER} DIRECTORY)

find_program(OPSY_NM
    NAMES ${CMAKE_CXX_COMPILER_TARGET}-nm arm-none-eabi-nm llvm-nm nm
    HINTS ${OPSY_TOOLCHAIN_BIN}
)

if(OPSY_NM)
    add_test(
        NAME opsy_no_heap_${OPSY_TARGET}
        COMMAND ${CMAKE_COMMAND}
                -DNM=${OPSY_NM}
                -DIMAGE=$<TARGET_FILE:opsy_qemu_tests>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_heap.cmake
    )
else()
    message(STATUS "no-heap check skipped: no nm found next to ${CMAKE_CXX_COMPILER}")
endif()

# The debugger-driven scenarios run against this same image: they stop it at
# chosen instructions to reach states the suite above cannot produce on its
# own. See tests/gdb/scenarios.cmake.
include(${CMAKE_CURRENT_SOURCE_DIR}/../gdb/scenarios.cmake)
