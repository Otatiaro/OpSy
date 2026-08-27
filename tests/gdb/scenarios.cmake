# Registers the GDB-driven scenarios as ctest tests.
#
# Included from tests/qemu/CMakeLists.txt, which builds the image they run
# against: the scenarios drive the same test image as the QEMU suite, so there
# is nothing separate to compile here.
#
# What the scenarios add over that suite is the debugger. The QEMU suite can
# only observe what the scheduler does on its own; these stop the CPU at a
# chosen instruction, write state the hardware would take weeks to reach,
# inject interrupts at the exact moment a race needs them, and inspect
# structures no running code can see. Each .gdb file in this directory is one
# scenario and says at its top what it exists to prove.
#
# Requires arm-none-eabi-gdb and a Python interpreter in addition to the
# qemu-system-arm the QEMU suite already needs. If either is missing the
# scenarios are skipped with a message rather than failing configuration: they
# are an addition to the suite, not a prerequisite for building it.

find_program(OPSY_ARM_GDB arm-none-eabi-gdb)
find_package(Python3 COMPONENTS Interpreter QUIET)

if(NOT OPSY_ARM_GDB OR NOT Python3_Interpreter_FOUND)
    message(STATUS "GDB scenarios skipped: arm-none-eabi-gdb and a Python interpreter are both needed")
    return()
endif()

find_program(OPSY_QEMU_SYSTEM_ARM qemu-system-arm)

if(NOT OPSY_QEMU_SYSTEM_ARM)
    message(STATUS "GDB scenarios skipped: qemu-system-arm not found on PATH")
    return()
endif()

# Being on PATH is not enough: a cross GDB shipped with a toolchain often
# needs runtime libraries the machine does not have, and fails on its first
# call rather than at install time. Running it once here turns that into a
# skip with a reason instead of every scenario failing identically.
execute_process(
    COMMAND ${OPSY_ARM_GDB} --version
    RESULT_VARIABLE opsy_gdb_status
    OUTPUT_QUIET
    ERROR_VARIABLE opsy_gdb_error
)

if(NOT opsy_gdb_status EQUAL 0)
    message(STATUS "GDB scenarios skipped: ${OPSY_ARM_GDB} does not run (${opsy_gdb_error})")
    return()
endif()

# The scenarios stop the CPU at named functions and read named variables, so
# they need those to still exist in the image. An optimised build inlines
# scheduler::take_mutex and scheduler::try_critical_section into their callers
# and drops the frames the scenarios walk to find a return address -- the
# breakpoints then never fire, and the scenario reports no verdict or fails on
# a state it could not reach. That is a limit of stopping at a function by
# name, not something to work around: there is no function left to stop at.
#
# So they are registered only for a build with no -O. The on-target suite next
# door has no such limit and is what covers the optimised configuration.
set(opsy_build_is_optimised FALSE)

if(DEFINED OPSY_OPTIMISATION AND NOT "${OPSY_OPTIMISATION}" MATCHES "-O0")
    set(opsy_build_is_optimised TRUE)
elseif("${CMAKE_CXX_FLAGS}" MATCHES "(^| )-O[123sgz]")
    set(opsy_build_is_optimised TRUE)
endif()

if(opsy_build_is_optimised)
    message(STATUS "GDB scenarios skipped: they need a build with no -O, which keeps the functions they stop at")
    return()
endif()

set(OPSY_GDB_DIR ${CMAKE_CURRENT_LIST_DIR})

# CONFIGURE_DEPENDS so that dropping a new scenario in this directory is
# enough: without it the glob is evaluated once at configure time and a later
# `cmake --build` never sees the new file.
file(GLOB OPSY_GDB_SCENARIOS CONFIGURE_DEPENDS ${OPSY_GDB_DIR}/*.gdb)

foreach(scenario ${OPSY_GDB_SCENARIOS})
    get_filename_component(scenario_name ${scenario} NAME_WE)

    add_test(
        NAME opsy_gdb_${scenario_name}_${OPSY_TARGET}
        COMMAND ${Python3_EXECUTABLE} ${OPSY_GDB_DIR}/run_scenario.py
                ${OPSY_QEMU_SYSTEM_ARM}
                ${OPSY_ARM_GDB}
                ${OPSY_QEMU_MACHINE}
                $<TARGET_FILE:opsy_qemu_tests>
                ${scenario}
    )

    # Every scenario opens the same fixed GDB port, and QEMU's stub takes one
    # client, so two running at once would fight over it. The lock serialises
    # them; ctest -j stays useful for everything else.
    set_tests_properties(opsy_gdb_${scenario_name}_${OPSY_TARGET} PROPERTIES
        TIMEOUT 180
        RESOURCE_LOCK opsy_gdb_port
    )
endforeach()
