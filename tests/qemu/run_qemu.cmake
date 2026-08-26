# Boots one test image under QEMU and turns its output into a ctest verdict.
#
# Invoked by CMakeLists.txt with -DQEMU_MACHINE=... -DIMAGE=... . Kept as a
# script rather than a raw COMMAND so the pass/fail rule lives in one place:
# QEMU collapses every non-zero application exit code onto a process status of
# 1, and an image that hangs or faults before reaching the end would otherwise
# exit 0 with a truncated log. The RESULT line is what settles it.

find_program(QEMU_EXECUTABLE qemu-system-arm)

if(NOT QEMU_EXECUTABLE)
    message(FATAL_ERROR "qemu-system-arm not found on PATH")
endif()

execute_process(
    COMMAND ${QEMU_EXECUTABLE}
            -machine ${QEMU_MACHINE}
            -kernel ${IMAGE}
            -nographic
            -semihosting-config enable=on,target=native
            -monitor none
            -serial none
    OUTPUT_VARIABLE qemu_output
    ERROR_VARIABLE  qemu_errors
    RESULT_VARIABLE qemu_status
    TIMEOUT 90
)

# QEMU writes semihosting output to its own stderr, not stdout, so the verdict
# has to be looked for across both.
set(combined "${qemu_output}${qemu_errors}")
message("${combined}")

if(combined MATCHES "RESULT: PASS")
    if(NOT qemu_status EQUAL 0)
        message(FATAL_ERROR "tests passed but QEMU exited with status ${qemu_status}")
    endif()
    return()
endif()

if(combined MATCHES "RESULT: FAIL")
    message(FATAL_ERROR "test failures reported by the image")
endif()

message(FATAL_ERROR
    "the image never reported a result (QEMU status ${qemu_status}) -- "
    "it faulted, hung, or never reached the end of the suite")
