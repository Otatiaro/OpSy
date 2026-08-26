# Host-side behavioural test suite for the portable half of OpSy.
#
# The Cortex-M suite one directory up is compile-only: it cross-compiles into
# a static library that is never linked or run, so it can assert about types
# but not about behaviour. Everything in OpSy that is plain C++ — the
# containers, the allocator, the numerics — builds and runs natively, and the
# cases here are the ones that would have caught the behavioural bugs that
# suite could not see.
#
# Built for a 32-bit host on purpose. utility/allocator.hpp static_asserts
# that sizeof(int) == sizeof(void*), which matches every Cortex-M target and
# no 64-bit host; a 32-bit build also keeps the pointer arithmetic under test
# the same width as on target. Set OPSY_HOST_TESTS_M32=OFF to build native
# width instead, which skips the allocator cases.
#
#   cmake -S tests/host -B build/host -G Ninja
#   cmake --build build/host
#   ctest --test-dir build/host --output-on-failure

cmake_minimum_required(VERSION 3.20)
project(opsy_host_tests LANGUAGES CXX)

include(CTest)

option(OPSY_HOST_TESTS_M32 "Build the host tests as 32-bit" ON)

set(OPSY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

set(OPSY_HOST_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/host_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_biquad.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_callback.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ellipsoid_fit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_embedded_list.cpp
)

if(OPSY_HOST_TESTS_M32)
    list(APPEND OPSY_HOST_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_allocator.cpp)
else()
    message(STATUS "OPSY_HOST_TESTS_M32=OFF: skipping the allocator cases, which need sizeof(int) == sizeof(void*)")
endif()

add_executable(opsy_host_tests ${OPSY_HOST_TEST_SOURCES})

target_include_directories(opsy_host_tests PRIVATE ${OPSY_ROOT} ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(opsy_host_tests PRIVATE cxx_std_23)

# Same strict set as the Cortex-M suite, so a warning cannot be introduced
# here and go unnoticed until the cross build runs.
target_compile_options(opsy_host_tests PRIVATE
    -Wall -Werror -Wpedantic -Wextra
    -Wshadow -Wcast-align -Wcast-qual -Wnull-dereference
    -Wconversion -Wsign-conversion -Wdouble-promotion
    -fno-exceptions -fno-rtti
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(opsy_host_tests PRIVATE
        -Wlogical-op
        -Wduplicated-cond
        -Wduplicated-branches
    )
endif()

if(OPSY_HOST_TESTS_M32)
    target_compile_options(opsy_host_tests PRIVATE -m32)
    target_link_options(opsy_host_tests PRIVATE -m32)
endif()

add_test(NAME opsy_host_tests COMMAND opsy_host_tests)
