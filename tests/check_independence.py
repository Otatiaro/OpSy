#!/usr/bin/env python3
"""Fail if a header that claims to stand alone has pulled in the scheduler.

Usage: check_independence.py <compiler> <repo-root> <flags-file> <directory>...

Parts of this repository are useful without the RTOS: the containers and
numerics in utility/, the algorithms built on them, the resumable interrupt
routines. Their READMEs say so, and someone reading that decides to use one in
a project that runs no scheduler at all.

Nothing enforced it. A header only has to include one thing that includes
opsy.hpp -- which is easy, since the umbrella is the convenient include -- and
the claim becomes false while every job stays green, because the test build
compiles those headers alongside the scheduler anyway. That is how
utility/interrupt_vector.hpp came to drag in the tasks, the locks and the
scheduler in order to name one function-pointer alias.

So the claim is checked the only way that survives: by asking the compiler
what each header actually includes, transitively, and failing on anything from
the scheduler's own set.

WHAT IS ALLOWED, and why it is not a loophole:

  cortex_m.hpp, isr_priority.hpp, config.hpp, opsy_assert.hpp, hooks.hpp

These describe the processor, the project's configuration and the tracing
seams. They are what a bare-metal project uses whether or not it runs a
scheduler, they pull in no scheduler themselves, and something has to provide
`assert` and the priority type. Depending on them does not tie a header to the
RTOS.
"""

import subprocess
import sys
from pathlib import Path


# The scheduler and the types that only exist because of it. A standalone
# header including any of these is no longer standalone.
SCHEDULER_HEADERS = {
    "opsy.hpp",
    "scheduler.hpp",
    "scheduler_inl.hpp",
    "task.hpp",
    "mutex.hpp",
    "isr_lock.hpp",
    "condition_variable.hpp",
    "critical_section.hpp",
    "embedded_list.hpp",
    "callback.hpp",
}


def dependencies_of(compiler: str, flags: list[str], root: Path, header: Path) -> set[str]:
    """Every header the compiler actually reads when this one is included."""
    probe = f'#include "{header.resolve().as_posix()}"\n'

    result = subprocess.run(
        [compiler, *flags, "-I", str(root), "-I", str(header.parent),
         "-MM", "-MF", "-", "-x", "c++", "-"],
        input=probe, capture_output=True, text=True,
    )

    if result.returncode != 0:
        # A header that does not compile on its own is not standalone either,
        # and saying which one and why is more use than a bare exit code.
        raise RuntimeError(f"{header} does not compile on its own:\n{result.stderr}")

    names = set()
    for token in result.stdout.replace("\\", " ").split():
        if token.endswith(".hpp"):
            names.add(Path(token).name)
    return names


def main() -> int:
    if len(sys.argv) < 5:
        sys.exit(__doc__)

    compiler, root, flags_file, *directories = sys.argv[1:]
    flags = Path(flags_file).read_text(encoding="utf-8").split()
    root_path = Path(root)

    failures: list[str] = []
    checked = 0

    for directory in directories:
        headers = sorted(Path(directory).glob("*.hpp"))
        if not headers:
            failures.append(f"{directory}: no headers found, so nothing was checked")
            continue

        for header in headers:
            try:
                found = dependencies_of(compiler, flags, root_path, header)
            except RuntimeError as error:
                failures.append(str(error))
                continue

            checked += 1
            pulled = sorted(found & SCHEDULER_HEADERS)
            if pulled:
                failures.append(
                    f"{header.as_posix()} pulls in the scheduler: {', '.join(pulled)}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        print(f"\n{len(failures)} header(s) are not standalone. Either include only what "
              f"the header needs, or move it out of a directory that claims to stand "
              f"alone.", file=sys.stderr)
        return 1

    print(f"{checked} header(s) stand alone")
    return 0


if __name__ == "__main__":
    sys.exit(main())
