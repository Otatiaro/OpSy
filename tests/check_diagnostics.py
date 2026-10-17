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

            // Enough for the storage's own bookkeeping, nowhere near enough
            // for a frame holding parameters, a loop counter and a resume
            // point. A smaller figure would be rejected by routine_storage's
            // own static_assert instead, which is a different diagnostic.
            opsy::utility::routine demo(opsy::utility::routine_storage<24>&, uint32_t count)
            {
                for (uint32_t i = 0; i < count; ++i)
                {
                    g_register = i;
                    co_await std::suspend_always{};
                }
            }

            static opsy::utility::routine_storage<24> g_storage;
            extern "C" void use() { auto r = demo(g_storage, 3); r.resume(); }
        """,
        # The same snippet with storage large enough, to show the diagnostic
        # is about the size and not about something the snippet does wrong.
        "control": ("routine_storage<24>", "routine_storage<256>"),
    },
    {
        "name": "a callable larger than the callback storage",
        "expect": "Cannot store the invokable in the callback",
        "source": """
            #include <callback.hpp>
            #include <cstdint>

            struct two_words { uint32_t a; uint32_t b; void operator()() const {} };
            extern "C" void use() { opsy::callback<void(), 1> c = two_words{}; c(); }
        """,
        "control": ("callback<void(), 1>", "callback<void(), 2>"),
    },
    {
        "name": "a callable needing more alignment than the callback storage",
        "expect": "needs more alignment than the callback storage provides",
        "source": """
            #include <callback.hpp>

            struct alignas(8) wide { unsigned char bytes[8]; void operator()() const {} };
            extern "C" void use() { opsy::callback<void(), 4> c = wide{}; c(); }
        """,
        "control": ("alignas(8)", "alignas(4)"),
    },
    {
        "name": "a memory-mapped register wider than the core can access atomically",
        "expect": "TInt is not lock-free on this target",
        "source": """
            #include <utility/memory.hpp>
            #include <cstdint>

            struct reg
            {
                uint64_t v_;
                constexpr reg(uint64_t value = 0) : v_{value} {}
                constexpr uint64_t value() const { return v_; }
                constexpr operator uint64_t() const { return v_; }
            };

            [[gnu::used]] opsy::utility::memory<uint64_t, reg> g_register;
        """,
        "control": ("uint64_t", "uint32_t"),
    },
]


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

        # The control: the same code without the one thing the diagnostic is
        # about has to build, or the case above proves nothing about it.
        before, after = case["control"]
        assert before in case["source"], case["name"]
        status, output = compile_snippet(compiler, flags, root_path, case["source"].replace(before, after))

        if status != 0:
            failures.append(
                f"{case['name']}: the control ({before} -> {after}) does not compile "
                f"either -- so the diagnostic above says nothing about it:\n{output}")

    if failures:
        print("\n\n".join(failures), file=sys.stderr)
        return 1

    print(f"{len(CASES)} diagnostic(s) still stop the build")
    return 0


if __name__ == "__main__":
    sys.exit(main())
