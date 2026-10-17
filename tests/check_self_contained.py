#!/usr/bin/env python3
"""Fail if a public header declares functions only another header defines.

Usage: check_self_contained.py <compiler> <repo-root> <flags-file>

The synchronisation primitives and the task call into the scheduler, and the
scheduler needs some of them complete, so where each member is defined is
constrained. A header whose inline members are defined somewhere it does not
include compiles cleanly on its own and emits calls nothing defines: the
program links only if another translation unit happened to include the
definitions and use the same members. An ISR file that includes
condition_variable.hpp and calls notify_one() is exactly the one that does not.

The compiler says nothing about it, so each header is compiled alone,
together with a snippet that uses its members, and every OpSy symbol the
object still needs must be provided by scheduler.cpp, which every OpSy program
links.
"""

import subprocess
import sys
import tempfile
from pathlib import Path


CASES = {
    "condition_variable.hpp": """
        void probe(opsy::condition_variable& c, opsy::mutex& m, opsy::isr_lock& l)
        {
            c.notify_one();
            c.notify_all();
            c.wait();
            c.wait(m);
            c.wait(l);
            (void) c.wait_for(opsy::duration{1});
            (void) c.wait_for(m, opsy::duration{1});
            (void) c.wait_for(l, opsy::duration{1});
        }
    """,
    "semaphore.hpp": """
        void probe(opsy::counting_semaphore<4>& s, opsy::binary_semaphore& b)
        {
            s.release();
            s.acquire();
            (void) s.try_acquire();
            (void) s.try_acquire_for(opsy::duration{1});
            b.release();
            b.acquire();
        }
    """,
    "queue.hpp": """
        void probe(opsy::queue<int, 4>& q)
        {
            (void) q.try_push(1);
            (void) q.try_emplace(2);
            q.push(3);
            (void) q.push_for(4, opsy::duration{1});
            (void) q.try_pop();
            (void) q.pop();
            (void) q.pop_for(opsy::duration{1});
        }
    """,
    "isr_lock.hpp": """
        void probe(opsy::isr_lock& l) { l.lock(); l.unlock(); }
    """,
    "mutex.hpp": """
        void probe(opsy::mutex& m) { m.lock(); (void) m.try_lock(); m.unlock(); }
    """,
    "critical_section.hpp": """
        void probe(opsy::critical_section* c) { c->~critical_section(); }
    """,
    "task.hpp": """
        void probe(opsy::task<64>& t)
        {
            (void) t.start([] {});
            t.priority(opsy::task_priority::high);
            t.set_name("probe");
            t.request_stop();
            (void) t.kill();
        }
    """,
}


def symbols(nm: str, obj: Path, which: str) -> set[str]:
    out = subprocess.run([nm, which, "--format=just-symbols", str(obj)],
                         capture_output=True, text=True, check=True).stdout
    return {s for s in out.split() if s.startswith("_ZN4opsy")}


def main() -> int:
    compiler, root, flags_file = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
    flags = flags_file.read_text().split()
    # The nm of the same toolchain: <prefix>nm next to <prefix>g++, llvm-nm
    # next to clang.
    name = Path(compiler).name
    if "clang" in name:
        nm = str(Path(compiler).with_name("llvm-nm"))
    else:
        nm = str(Path(compiler).with_name(name.replace("g++", "nm")))

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        def compile_(source: Path, obj: Path) -> None:
            result = subprocess.run([compiler, *flags, "-I", str(root), "-c", str(source), "-o", str(obj)],
                                    capture_output=True, text=True)
            if result.returncode != 0:
                raise RuntimeError("%s does not compile:\n%s" % (source.name, result.stderr))

        scheduler = tmp / "scheduler.o"
        compile_(root / "scheduler.cpp", scheduler)
        provided = symbols(nm, scheduler, "--defined-only")

        for header, body in CASES.items():
            source = tmp / ("probe_" + header.replace(".hpp", ".cpp"))
            source.write_text('#include "%s"\n%s' % (header, body))
            obj = source.with_suffix(".o")
            compile_(source, obj)
            missing = symbols(nm, obj, "--undefined-only") - provided
            if missing:
                failures.append("%s alone leaves undefined:\n    %s" % (
                    header, "\n    ".join(sorted(missing))))

    for failure in failures:
        print(failure)
    if not failures:
        print("%d headers are self-contained" % len(CASES))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
