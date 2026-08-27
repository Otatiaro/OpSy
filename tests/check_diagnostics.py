#!/usr/bin/env python3
"""Fail if a diagnostic that should stop the build no longer does.

Usage: check_diagnostics.py <compiler> <repo-root> <flags-file>

Some mistakes are caught by refusing to compile rather than by failing at
run time. That is worth more than an assert -- nobody has to reach the line
for it to be found -- but it is also invisible: a diagnostic that stops
triggering leaves no trace, and everything goes on building. Only a build
that is *supposed* to fail can tell the difference.

Each case below is a snippet that must not compile, together with what the
error has to mention. A case that compiles is a failure; so is one that fails
for some unrelated reason, which is why the message is checked and not just
the exit status.

These need an optimised build: the diagnostics they exercise fold a size the
compiler substitutes, which without -O is still a runtime value. The flags
passed in therefore include one.
"""

import subprocess
import sys
import tempfile
from pathlib import Path


CASES = [
    {
        "name": "a routine whose frame does not fit its storage",
        "expect": "the_routine_frame_does_not_fit_its_storage",
        "source": """
            #include <utility/routine.hpp>
            #include <cstdint>

            static volatile uint32_t g_register;

            // Eight bytes cannot hold a frame with parameters, a loop counter
            // and a resume point.
            opsy::utility::routine demo(opsy::utility::routine_storage<8>&, uint32_t count)
            {
                for (uint32_t i = 0; i < count; ++i)
                {
                    g_register = i;
                    co_await std::suspend_always{};
                }
            }

            static opsy::utility::routine_storage<8> g_storage;
            extern "C" void use() { auto r = demo(g_storage, 3); r.resume(); }
        """,
    },
]

# The same snippet with storage large enough, to show the diagnostic is about
# the size and not about something the snippet does wrong in general.
CONTROL_STORAGE = 256


def compile_snippet(compiler: str, flags: list[str], root: Path, source: str) -> tuple[int, str]:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "snippet.cpp"
        path.write_text(source, encoding="utf-8")

        result = subprocess.run(
            [compiler, *flags, "-I", str(root), "-c", str(path),
             "-o", str(Path(directory) / "snippet.o")],
            capture_output=True, text=True,
        )
        return result.returncode, result.stdout + result.stderr


def main() -> int:
    if len(sys.argv) != 4:
        sys.exit(__doc__)

    compiler, root, flags_file = sys.argv[1:]
    flags = Path(flags_file).read_text(encoding="utf-8").split()
    root_path = Path(root)

    failures: list[str] = []

    for case in CASES:
        status, output = compile_snippet(compiler, flags, root_path, case["source"])

        if status == 0:
            failures.append(
                f"{case['name']}: compiled, but the diagnostic should have stopped it")
        elif case["expect"] not in output:
            failures.append(
                f"{case['name']}: failed to compile, but not for the expected reason.\n"
                f"  expected the error to mention: {case['expect']}\n"
                f"  got:\n{output}")

        # The control: the same code with room to spare has to build, or the
        # case above proves nothing about the size.
        control = case["source"].replace("routine_storage<8>", f"routine_storage<{CONTROL_STORAGE}>")
        status, output = compile_snippet(compiler, flags, root_path, control)

        if status != 0:
            failures.append(
                f"{case['name']}: the control, with storage of {CONTROL_STORAGE} bytes, "
                f"does not compile either -- so the diagnostic above says nothing "
                f"about the size:\n{output}")

    if failures:
        print("\n\n".join(failures), file=sys.stderr)
        return 1

    print(f"{len(CASES)} diagnostic(s) still stop the build")
    return 0


if __name__ == "__main__":
    sys.exit(main())
