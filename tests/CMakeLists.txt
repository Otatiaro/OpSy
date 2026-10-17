# Compile-only sanity test for the OpSy public API and utility headers.
#
# Builds scheduler.cpp + sanity.cpp + utility_sanity.cpp into static
# libraries, one per build configuration, so the entire API surface is
# exercised at compile time on every Cortex-M target supported by OpSy.
# utility_sanity.cpp also runs the
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

include(${CMAKE_CURRENT_SOURCE_DIR}/warnings.cmake)

# OpSy targets C++23, and is kept building under C++26 as that lands: the CI
# builds both so a construct C++26 changes or removes is caught here rather
# than by a user on a newer toolchain. Override with -DOPSY_CXX_STANDARD=26.
if(NOT DEFINED OPSY_CXX_STANDARD)
    set(OPSY_CXX_STANDARD 23 CACHE STRING "C++ standard to build with: 23 or 26")
endif()

# The same sources once per configuration a project ships with: debug and
# release, the latter with and without asserts. The optimiser decides what
# several warnings see, and NDEBUG removes code, so one configuration passing
# says little about the others.
set(OPSY_SANITY_CONFIGS
    "O0|-O0"
    "Os|-Os"
    "O2|-O2"
    "Os_ndebug|-Os -DNDEBUG"
    "O2_ndebug|-O2 -DNDEBUG"
)

foreach(config IN LISTS OPSY_SANITY_CONFIGS)
    string(REPLACE "|" ";" config "${config}")
    list(POP_FRONT config name)
    separate_arguments(config)
    set(target opsy_sanity_${name})

    add_library(${target} STATIC
        ${OPSY_ROOT}/scheduler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/sanity.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/utility_sanity.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/readme_examples.cpp
    )

    target_include_directories(${target} PUBLIC ${OPSY_ROOT})

    set_target_properties(${target} PROPERTIES
        CXX_STANDARD ${OPSY_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    target_compile_options(${target} PRIVATE
        ${OPSY_CPU_FLAGS}
        ${config}
        -fno-exceptions -fno-rtti
        -ffunction-sections -fdata-sections
    )
    opsy_strict_warnings(${target})
endforeach()

# Standalone directories: the check that they really are.
#
# utility/ and algorithms/ are useful without the RTOS, and their
# READMEs say so. Nothing enforced it: this build compiles them alongside
# scheduler.cpp, so a header that started pulling in opsy.hpp would go on
# passing every job while the claim in its README quietly became false. That
# is not hypothetical -- utility/interrupt_vector.hpp was in exactly that
# state, dragging in the tasks, the locks and the scheduler in order to name
# one function-pointer alias.
#
# So it is asked of the compiler instead: check_independence.py reads what
# each header transitively includes and fails on anything from the
# scheduler's own set. See the script for what is allowed, and why.
find_package(Python3 COMPONENTS Interpreter QUIET)

# Outside the check below on purpose: the CI runs ctest on this build
# unconditionally, and without enable_testing() there is no CTestTestfile at
# all -- ctest then fails with "cannot find file", which says nothing about a
# missing interpreter. With it, a build without Python simply has no tests.
enable_testing()

if(Python3_Interpreter_FOUND)

    # The flags go through a file because they must reach the compiler exactly
    # as they are here -- a target's flags are a list, and flattening one into
    # a command line loses the boundaries between arguments.
    # -std explicitly: the standard normally reaches the compiler through the
    # target's CXX_STANDARD property, which a standalone invocation of the
    # compiler does not see. Without it the check would compile the headers
    # under the compiler's default standard -- older than what OpSy requires,
    # and <coroutine> refuses to be included there at all.
    # CMAKE_CXX_FLAGS as well as the per-CPU ones: the toolchain file puts
    # --target=arm-none-eabi there for clang, and a compiler invoked without it
    # cross-compiles for the host instead -- where none of these headers are
    # meant to build, so every one of them would be reported as broken.
    separate_arguments(OPSY_INDEPENDENCE_BASE NATIVE_COMMAND "${CMAKE_CXX_FLAGS}")

    set(OPSY_INDEPENDENCE_FLAGS
        -std=c++${OPSY_CXX_STANDARD}
        ${OPSY_INDEPENDENCE_BASE}
        ${OPSY_CPU_FLAGS}
        -fno-exceptions -fno-rtti)
    list(JOIN OPSY_INDEPENDENCE_FLAGS " " OPSY_INDEPENDENCE_FLAGS_TEXT)
    file(GENERATE
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/independence_flags.txt
        CONTENT "${OPSY_INDEPENDENCE_FLAGS_TEXT}")

    # Diagnostics meant to stop the build are checked by trying to build
    # something that must not compile. A diagnostic that stops firing leaves
    # no trace otherwise -- everything simply goes on building. Needs -O: the
    # size it folds is not a constant without one, which is also why this is
    # a separate invocation rather than a flag added to the check above.
    set(OPSY_DIAGNOSTIC_FLAGS ${OPSY_INDEPENDENCE_FLAGS} -O1)
    list(JOIN OPSY_DIAGNOSTIC_FLAGS " " OPSY_DIAGNOSTIC_FLAGS_TEXT)
    file(GENERATE
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/diagnostic_flags.txt
        CONTENT "${OPSY_DIAGNOSTIC_FLAGS_TEXT}")

    add_test(
        NAME opsy_diagnostics_${OPSY_TARGET}
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/check_diagnostics.py
                ${CMAKE_CXX_COMPILER}
                ${OPSY_ROOT}
                ${CMAKE_CURRENT_BINARY_DIR}/diagnostic_flags.txt
    )

    # Every public header usable on its own: whatever it declares is defined
    # by the header itself or by scheduler.cpp.
    add_test(
        NAME opsy_self_contained_${OPSY_TARGET}
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/check_self_contained.py
                ${CMAKE_CXX_COMPILER}
                ${OPSY_ROOT}
                ${CMAKE_CURRENT_BINARY_DIR}/diagnostic_flags.txt
    )

    add_test(
        NAME opsy_independence_${OPSY_TARGET}
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/check_independence.py
                ${CMAKE_CXX_COMPILER}
                ${OPSY_ROOT}
                ${CMAKE_CURRENT_BINARY_DIR}/independence_flags.txt
                ${OPSY_ROOT}/utility
                ${OPSY_ROOT}/algorithms
    )
else()
    message(STATUS "independence check skipped: no Python interpreter found")
endif()
