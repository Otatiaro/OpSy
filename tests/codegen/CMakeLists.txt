# Codegen checks: builds probes at -O2 and asserts on the instructions emitted.
#
# The third thing the other two suites cannot do. The cross build proves the
# code compiles but is built without -O; the QEMU suite runs it but only shows
# an optimisation bug if the compiler happened to take the dangerous
# transformation. Here the disassembly itself is the assertion, so the
# volatile-MMIO and stack-alignment properties are checked directly.
#
#   cmake -S tests/codegen -B build-codegen -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE="$PWD/tests/cortex-m-toolchain.cmake" \
#         -DOPSY_COMPILER=gcc -DOPSY_TARGET=m4
#   cmake --build build-codegen
#   ctest --test-dir build-codegen --output-on-failure

cmake_minimum_required(VERSION 3.20)
project(opsy_codegen LANGUAGES CXX)

include(CTest)

if(NOT DEFINED OPSY_TARGET)
    set(OPSY_TARGET "m4" CACHE STRING "Cortex-M target: m3, m4, m7, m33")
endif()

if(OPSY_TARGET STREQUAL "m3")
    set(OPSY_CPU_FLAGS -mcpu=cortex-m3 -mthumb -mfloat-abi=soft)
elseif(OPSY_TARGET STREQUAL "m4")
    set(OPSY_CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
elseif(OPSY_TARGET STREQUAL "m7")
    set(OPSY_CPU_FLAGS -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)
elseif(OPSY_TARGET STREQUAL "m33")
    set(OPSY_CPU_FLAGS -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard)
else()
    message(FATAL_ERROR "Unknown OPSY_TARGET: ${OPSY_TARGET}")
endif()

set(OPSY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

if(NOT DEFINED OPSY_CXX_STANDARD)
    set(OPSY_CXX_STANDARD 23 CACHE STRING "C++ standard to build with: 23 or 26")
endif()

# scheduler.cpp comes along for SVC_Handler, whose prologue is one of the
# things checked.
add_library(opsy_codegen STATIC
    ${OPSY_ROOT}/scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/codegen_probe.cpp
)

target_include_directories(opsy_codegen PUBLIC ${OPSY_ROOT})

set_target_properties(opsy_codegen PROPERTIES
    CXX_STANDARD ${OPSY_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# -O2 and NDEBUG on purpose: this is the configuration the checks are about.
# -fno-lto so the probes survive as inspectable symbols.
target_compile_options(opsy_codegen PRIVATE
    ${OPSY_CPU_FLAGS}
    -O2 -DNDEBUG -fno-lto
    -Wall -Werror -Wpedantic -Wextra
    -Wshadow -Wcast-align -Wcast-qual -Wnull-dereference
    -Wconversion -Wsign-conversion -Wdouble-promotion
    -Wno-psabi
    -fno-exceptions -fno-rtti
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(opsy_codegen PRIVATE -Wlogical-op -Wduplicated-cond -Wduplicated-branches)
endif()

# objdump sits next to the compiler; derive it rather than assuming a PATH.
get_filename_component(OPSY_TOOLCHAIN_BIN ${CMAKE_CXX_COMPILER} DIRECTORY)
get_filename_component(OPSY_TOOLCHAIN_PREFIX ${CMAKE_CXX_COMPILER} NAME_WE)
string(REGEX REPLACE "(g\\+\\+|clang\\+\\+)$" "" OPSY_TOOLCHAIN_PREFIX ${OPSY_TOOLCHAIN_PREFIX})

find_program(OPSY_OBJDUMP
    NAMES ${OPSY_TOOLCHAIN_PREFIX}objdump arm-none-eabi-objdump llvm-objdump objdump
    HINTS ${OPSY_TOOLCHAIN_BIN}
)

if(NOT OPSY_OBJDUMP)
    message(FATAL_ERROR "no objdump found next to ${CMAKE_CXX_COMPILER}")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME opsy_codegen_${OPSY_TARGET}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen.py
            ${OPSY_OBJDUMP}
            $<TARGET_FILE:opsy_codegen>
)
