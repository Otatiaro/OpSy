#!/usr/bin/env python3
"""Disassemble the -O2 probes and assert on the instructions produced.

These properties cannot be observed by running the code: they are about what
the optimiser was, and was not, allowed to do to memory-mapped accesses. The
cross build compiles without -O and so proves nothing here, and emulation only
shows such a bug if the compiler happened to take the dangerous transformation.

Usage: check_codegen.py <objdump> <object-file>
"""

import re
import subprocess
import sys


def disassemble(objdump: str, object_file: str) -> str:
    result = subprocess.run(
        [objdump, "-d", "--no-show-raw-insn", object_file],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        sys.exit(f"objdump failed ({result.returncode}):\n{result.stderr}")
    return result.stdout


def body_of(disassembly: str, name: str) -> list[str]:
    """Lines of one function, from its label to the next blank line."""
    lines = disassembly.splitlines()
    label = re.compile(rf"<{re.escape(name)}>:$")
    for index, line in enumerate(lines):
        if label.search(line):
            body = []
            for following in lines[index + 1:]:
                if not following.strip():
                    break
                body.append(following)
            return body
    return []


def count(body: list[str], mnemonic: str) -> int:
    pattern = re.compile(rf"\b{mnemonic}[a-z0-9.]*\b")
    return sum(1 for line in body if pattern.search(line))


def in_order(body: list[str], *mnemonics: str) -> bool:
    """Whether the mnemonics appear in this order, not necessarily adjacent."""
    patterns = [re.compile(rf"\s{m}[a-z0-9.]*\s") for m in mnemonics]
    position = 0
    for line in body:
        if position < len(patterns) and patterns[position].search(line):
            position += 1
    return position == len(patterns)


def main() -> int:
    if len(sys.argv) != 3:
        sys.exit(__doc__)

    disassembly = disassemble(sys.argv[1], sys.argv[2])
    failures: list[str] = []

    def require(name: str) -> list[str]:
        body = body_of(disassembly, name)
        if not body:
            failures.append(f"{name}: symbol not found in the disassembly")
        return body

    # Two reads of the same register must stay two loads. Folding them is what
    # made a delay loop built on systick_count() never terminate.
    body = require("probe_two_reads_stay_two")
    if body and count(body, "ldr") < 2:
        failures.append(
            f"probe_two_reads_stay_two: {count(body, 'ldr')} load(s), expected >= 2 -- "
            "the two reads of the SysTick register were folded into one, "
            "so the access is not volatile")

    # The spin must reload inside the loop; hoisting the read out makes it
    # non-terminating.
    body = require("probe_spin_on_register")
    if body:
        loads = count(body, "ldr")
        branches = sum(1 for line in body if re.search(r"\bb[a-z.]*\s+[0-9a-f]+ <", line))
        if loads < 2 or branches < 1:
            failures.append(
                f"probe_spin_on_register: {loads} load(s), {branches} branch(es) -- "
                "the register read was hoisted out of the loop, "
                "which makes the spin non-terminating")

    # enable_systick writes CTRL off, LOAD, VAL, CTRL on. The first store has
    # no visible effect on any C++ object, so a non-volatile access lets it be
    # dropped as dead -- leaving the timer running while it is reprogrammed.
    body = require("probe_stores_are_not_eliminated")
    if body and count(body, "str") < 4:
        failures.append(
            f"probe_stores_are_not_eliminated: {count(body, 'str')} store(s), expected >= 4 "
            "(CTRL off, LOAD, VAL, CTRL on) -- a store to a memory-mapped "
            "register was eliminated as dead")

    # Exception entry aligns MSP to 8; pushing an odd number of registers
    # leaves it 4-mod-8 across the bl, which breaks the 8-byte alignment AAPCS
    # requires at a public interface -- the user-supplied hooks included.
    body = body_of(disassembly, "SVC_Handler")
    if body:
        pushes = [line for line in body if re.search(r"\bpush\b", line)]
        if not pushes:
            failures.append("SVC_Handler: no push found, cannot check stack alignment")
        else:
            registers = re.search(r"\{([^}]*)\}", pushes[0])
            pushed = len(registers.group(1).split(",")) if registers else 0
            if pushed % 2:
                failures.append(
                    f"SVC_Handler: pushes {pushed} register(s) -- an odd count leaves "
                    "MSP 4-mod-8 across the bl, breaking the alignment AAPCS requires")

    # A store-exclusive may write memory, or may not. The read after it must go
    # to memory rather than be answered from the value stored before; and that
    # earlier store must stay, since a failed strex leaves it in place.
    body = require("probe_strex_then_read")
    if body and not in_order(body, "str", "strex", "ldr"):
        failures.append(
            "probe_strex_then_read: expected str then strex then ldr -- either the "
            "read is answered from a stale value, or the store before the strex "
            "was dropped as if the strex always succeeded")

    # Barriers the architecture requires after a write whose effect the
    # following instructions rely on.
    for name, sequence, why in (
        ("probe_pend_sv_is_synchronised", ("str", "dsb", "isb"),
         "PendSV may not be taken before the caller's next instruction"),
        ("probe_disable_interrupt_is_synchronised", ("str", "dsb", "isb"),
         "the interrupt may still be taken after disable_interrupt returns"),
        ("probe_control_write_is_synchronised", ("msr", "isb"),
         "instructions after the CONTROL write may run with the old stack or privilege"),
        ("probe_vtor_move_is_synchronised", ("str", "dsb"),
         "an exception may still be vectored through the old table"),
    ):
        body = require(name)
        if body and not in_order(body, *sequence):
            failures.append(f"{name}: expected {' then '.join(sequence)} -- {why}")

    for failure in failures:
        print(f"FAIL {failure}", file=sys.stderr)

    if failures:
        print(f"\n{len(failures)} codegen check(s) failed", file=sys.stderr)
        return 1

    print("codegen checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
