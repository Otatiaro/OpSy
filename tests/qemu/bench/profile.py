#!/usr/bin/env python3
"""Instructions executed between the bench image's markers, per scenario and per function.

QEMU does not model cycles: with -icount it executes one instruction per tick,
and the DWT the image would read is absent. What it gives instead is exact and
repeatable: every instruction executed, in order. This script runs the bench
image with one instruction per translation block and the execution trace on
(`-accel tcg,one-insn-per-tb=on -d exec,nochain`), so each trace line is one
instruction; it counts them between bench::begin<S>() and bench::end<S>() for
each scenario S, and attributes them to the functions they belong to. One
instruction per block, rather than the translated blocks' sizes: with -icount
a block is cut where the instruction budget runs out, so the same start
address is translated at several lengths, and a count by block is wrong.

    profile.py --image build/opsy_qemu_bench.elf --machine mps2-an505
               [--qemu qemu-system-arm] [--nm arm-none-eabi-nm] [--top 12]
               [--json out.json] [--baseline tests/qemu/bench/baseline-m33.json]
               [--tolerance 0.05] [--trace existing-trace.log]

With --baseline, the medians are compared to the file's and the exit status is
1 when one grew by more than the tolerance; a drop beyond it is reported too,
so the baseline gets updated with the gain. --json writes the medians in the
baseline's format.
"""

import argparse
import bisect
import json
import re
import statistics
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path

EXEC = re.compile(r"^Trace \d+: \S+ \[[0-9a-f]+/([0-9a-f]+)/")
SYMBOL = re.compile(r"^([0-9a-f]+) [TtWw] (.+)$")
BEGIN = re.compile(r"^void bench::begin<\(bench::scenario\)(\d+)>\(\)$")
END = re.compile(r"^void bench::end<\(bench::scenario\)(\d+)>\(\)$")
RESULT_LINE = re.compile(r"^BENCH (\S+) ")


def run_qemu(qemu, machine, image, trace_path):
    command = [
        qemu, "-machine", machine, "-accel", "tcg,one-insn-per-tb=on", "-icount", "shift=5,sleep=off",
        "-kernel", str(image), "-nographic", "-semihosting-config", "enable=on,target=native",
        "-monitor", "none", "-serial", "none", "-d", "exec,nochain", "-D", str(trace_path),
    ]
    completed = subprocess.run(command, capture_output=True, text=True, timeout=600)
    output = completed.stdout + completed.stderr
    if "RESULT: PASS" not in output:
        sys.exit(f"the bench image did not pass under QEMU:\n{output}")
    return output


def read_symbols(nm, image):
    """Sorted (address, demangled name) of the image's functions."""
    text = subprocess.run([nm, "-n", "-C", "--defined-only", str(image)], capture_output=True, text=True, check=True).stdout
    symbols = []
    for line in text.splitlines():
        match = SYMBOL.match(line)
        if match:
            symbols.append((int(match.group(1), 16), match.group(2)))
    symbols.sort()
    return symbols


def function_of(symbols, addresses):
    def lookup(pc):
        index = bisect.bisect_right(addresses, pc) - 1
        return symbols[index][1] if index >= 0 else f"0x{pc:08x}"
    return lookup


def parse_trace(trace_path):
    """The executed instructions' addresses, in order."""
    executed = []
    with open(trace_path, encoding="utf-8", errors="replace") as trace:
        for line in trace:
            match = EXEC.match(line)
            if match:
                executed.append(int(match.group(1), 16))
    return executed


def count(executed, symbols, markers):
    """Per scenario: one (total, Counter per function) per iteration, in order."""
    addresses = [address for address, _ in symbols]
    lookup = function_of(symbols, addresses)
    begins = {address: s for (address, kind, s) in markers if kind == "begin"}
    ends = {address: s for (address, kind, s) in markers if kind == "end"}
    marker_addresses = set(begins) | set(ends)

    iterations = defaultdict(list)
    running = None
    total = 0
    per_function = Counter()
    for pc in executed:
        if pc in begins:
            # A begin while running restarts: the sleep scenario begins at
            # every tick and keeps the last one.
            running, total, per_function = begins[pc], 0, Counter()
            continue
        if pc in ends:
            if running == ends[pc]:
                iterations[running].append((total, per_function))
            running = None
            continue
        if running is None:
            continue
        total += 1
        if pc not in marker_addresses:
            per_function[lookup(pc)] += 1
    return iterations


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--machine", required=True)
    parser.add_argument("--qemu", default="qemu-system-arm")
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("--top", type=int, default=12, help="functions shown per scenario")
    parser.add_argument("--json", type=Path, help="write the medians here")
    parser.add_argument("--baseline", type=Path, help="compare the medians to this file")
    parser.add_argument("--tolerance", type=float, default=0.05)
    parser.add_argument("--trace", type=Path, help="an existing trace to read instead of running QEMU")
    args = parser.parse_args()

    symbols = read_symbols(args.nm, args.image)
    markers = []
    for address, name in symbols:
        for kind, pattern in (("begin", BEGIN), ("end", END)):
            match = pattern.match(name)
            if match:
                markers.append((address, kind, int(match.group(1))))
    if not markers:
        sys.exit("no bench::begin/end markers in the image")

    if args.trace:
        trace_path = args.trace
        output = ""
    else:
        trace_path = Path(tempfile.mkdtemp()) / "trace.log"
        output = run_qemu(args.qemu, args.machine, args.image, trace_path)

    # The image prints one BENCH line per scenario, in enum order: that is
    # where the names come from.
    names = [match.group(1) for match in map(RESULT_LINE.match, output.splitlines()) if match]

    executed = parse_trace(trace_path)
    iterations = count(executed, symbols, markers)

    medians = {}
    for scenario in sorted(iterations):
        runs = iterations[scenario]
        totals = [total for total, _ in runs]
        name = names[scenario] if scenario < len(names) else f"scenario {scenario}"
        median = int(statistics.median_low(totals))
        medians[name] = median
        median_run = next(functions for total, functions in runs if total == median)
        # What the measurement itself costs: the hooks' marks and the markers'
        # neighbours, in bench:: -- on a board they cost cycles the same way.
        overhead = sum(instructions for function, instructions in median_run.items() if function.startswith("bench::"))
        print(f"{name}: {median} instructions, {median - overhead} without the bench's own "
              f"(min {min(totals)}, max {max(totals)}, {len(totals)} iterations)")
        for function, instructions in median_run.most_common(args.top):
            print(f"    {instructions:6d}  {function}")

    if args.json:
        args.json.write_text(json.dumps(medians, indent=2) + "\n", newline="\n")

    if args.baseline:
        baseline = json.loads(args.baseline.read_text())
        worse = False
        for name, median in medians.items():
            reference = baseline.get(name)
            if reference is None:
                print(f"{name}: not in the baseline")
                continue
            change = (median - reference) / reference
            if change > args.tolerance:
                print(f"{name}: {median} instructions, {reference} in the baseline, +{change:.1%}: REGRESSION")
                worse = True
            elif change < -args.tolerance:
                print(f"{name}: {median} instructions, {reference} in the baseline, {change:.1%}: better, update the baseline")
        if worse:
            sys.exit(1)


if __name__ == "__main__":
    main()
