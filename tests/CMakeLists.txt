# Compile-only sanity test for the OpSy public API and utility headers.
#
# Builds scheduler.cpp + sanity.cpp + utility_sanity.cpp into a static
# library so the entire API surface is exercised at compile time on every
# Cortex-M target supported by OpSy. utility_sanity.cpp also runs the
# constexpr/static_assert tests embedded in utility/*.hpp by instantiating
# every template with valid configurations. No executable is produced (no
# startup, no linker script needed).
#
# Drive it with -DOPSY_TARGET=m3|m4|m7|m33 from the CI matrix.

cmake_minimum_required(VERSION 3.20)
project(opsy_sanity LANGUAGES CXX)

if(NOT DEFINED OPSY_TARGET)
    set(OPSY_TARGET "m33" CACHE STRING "Cortex-M target: m3, m4, m7, m33")
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
    message(FATAL_ERROR "Unknown OPSY_TARGET: ${OPSY_TARGET} (expected m3, m4, m7 or m33)")
endif()

set(OPSY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(opsy_sanity STATIC
    ${OPSY_ROOT}/scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sanity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utility_sanity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/readme_examples.cpp
)

target_include_directories(opsy_sanity PUBLIC ${OPSY_ROOT})

# OpSy targets C++23, and is kept building under C++26 as that lands: the CI
# builds both so a construct C++26 changes or removes is caught here rather
# than by a user on a newer toolchain. Override with -DOPSY_CXX_STANDARD=26.
if(NOT DEFINED OPSY_CXX_STANDARD)
    set(OPSY_CXX_STANDARD 23 CACHE STRING "C++ standard to build with: 23 or 26")
endif()

set_target_properties(opsy_sanity PROPERTIES
    CXX_STANDARD ${OPSY_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Optimisation level. The default (empty) leaves it to the build type, but the
# CI drives this explicitly: the MMIO-volatile and missing-"memory"-clobber
# class of bug only exists once the optimiser is allowed to reorder, cache or
# eliminate, so a suite built without -O proves nothing about them.
if(DEFINED OPSY_OPTIMISATION)
    add_compile_options(${OPSY_OPTIMISATION})
endif()

target_compile_options(opsy_sanity PRIVATE
    ${OPSY_CPU_FLAGS}
    -Wall -Werror -Wpedantic -Wextra
    -Wshadow -Wcast-align -Wcast-qual -Wnull-dereference
    -Wconversion -Wsign-conversion -Wdouble-promotion
    -Wno-psabi
    -fno-exceptions -fno-rtti
    -ffunction-sections -fdata-sections
)

# GCC-only diagnostics. clang does not recognise these flags and would
# error out on them, so add them only when the compiler is actually GCC.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(opsy_sanity PRIVATE
        -Wlogical-op
        -Wduplicated-cond
        -Wduplicated-branches
    )
endif()
