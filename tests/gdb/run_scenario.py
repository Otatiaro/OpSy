#!/usr/bin/env python3
"""Run one GDB scenario against the test image under QEMU.

The other suites can only observe what the scheduler does on its own. This one
drives the emulator: it stops the CPU at a chosen instruction, writes state the
hardware would take weeks to reach, injects interrupts at the exact moment a
race needs them, and inspects invariants that no running code can see.

That covers the cases the QEMU suite lists as untestable — the narrow races,
and the 64-bit tick counter's low-word wrap, which is ~49.7 days away at 1 ms.

Usage: run_scenario.py <qemu> <gdb> <machine> <image> <script.gdb>

The scenario decides pass/fail by printing SCENARIO: PASS or SCENARIO: FAIL,
the same way the on-target images report through semihosting: a scenario that
dies without printing must not be read as success.
"""

import re
import subprocess
import sys
import tempfile
import time


GDB_PORT = 1234

# How long QEMU is given to open its GDB port before the session connects.
# Not measured by probing the port: QEMU's GDB stub accepts a single client, so
# a probe that connects takes the one slot and the real session is refused,
# which looks exactly like the scenario hanging. Starting late costs nothing
# because -S freezes the CPU at reset until the session says otherwise.
QEMU_SETTLE_SECONDS = 2

# How long the whole scenario may take. Generous: each `continue` runs the
# image up to a breakpoint, and a scenario may single-step through code.
SCENARIO_TIMEOUT_SECONDS = 120


def main() -> int:
    if len(sys.argv) != 6:
        sys.exit(__doc__)

    qemu, gdb, machine, image, script = sys.argv[1:]

    # QEMU's output goes to a file, never to a pipe. The image writes to it
    # through semihosting for as long as it runs, and a pipe nobody reads
    # fills up (64 KiB is typical) and blocks the writer — QEMU would then
    # stop dead with the session still waiting on a target that never moves.
    with tempfile.TemporaryFile(mode="w+") as emulator_output:
        # -S freezes the CPU at reset, so the scenario gets to set breakpoints
        # before a single instruction of the image has run.
        emulator = subprocess.Popen(
            [qemu, "-machine", machine, "-kernel", image, "-nographic",
             "-semihosting-config", "enable=on,target=native",
             "-monitor", "none", "-serial", "none",
             "-S", "-gdb", f"tcp::{GDB_PORT}"],
            stdout=emulator_output, stderr=subprocess.STDOUT,
        )

        try:
            time.sleep(QEMU_SETTLE_SECONDS)

            session = subprocess.run(
                [gdb, "-batch", "-nx", "-x", script, image],
                capture_output=True, text=True,
                timeout=SCENARIO_TIMEOUT_SECONDS,
            )
            output = session.stdout + session.stderr
        except subprocess.TimeoutExpired:
            emulator_output.seek(0)
            print(emulator_output.read(), file=sys.stderr)
            print(f"FAIL the scenario did not finish within "
                  f"{SCENARIO_TIMEOUT_SECONDS}s", file=sys.stderr)
            return 1
        finally:
            emulator.kill()
            try:
                emulator.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass

    # GDB's own noise buries the scenario's output; keep the lines it printed.
    for line in output.splitlines():
        if re.match(r"^\s*(SCENARIO:|CHECK |INFO )", line):
            print(line)

    if "SCENARIO: FAIL" in output:
        print("\n--- full session ---\n" + output, file=sys.stderr)
        return 1

    if "SCENARIO: PASS" not in output:
        print("\n--- full session ---\n" + output, file=sys.stderr)
        print("FAIL the scenario reported no verdict", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
