#!/usr/bin/env python3
"""Runs clang-tidy over OpSy as the suites compile it.

    tidy.py --clang-tidy CLANG_TIDY --build BUILD [--build BUILD...]
            [--headers DIR...] [--jobs N] [FILE...]

Each BUILD is a build directory configured with
CMAKE_EXPORT_COMPILE_COMMANDS=ON. A source file is checked once per set of
preprocessor definitions and include directories it is built with: those
change what clang-tidy sees, the CPU and the optimisation level do not. Each
header of a DIR is checked on its own, once per such set of the first BUILD.
Clang
does not find the headers of a GCC toolchain by itself, so each compiler is
asked for its target and its include directories, which are passed to
clang-tidy instead of clang's own.

The checks and the files they cover are those of the .clang-tidy files. The
exit status is 1 when clang-tidy reports anything.
"""

import argparse
import concurrent.futures
import json
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Compiler options that change what clang-tidy sees, besides definitions and
# include directories.
TARGET = ("-std=", "-mcpu=", "-mthumb", "-mfloat-abi=", "-mfpu=", "-m32")
KEPT = ("-I", "-isystem", "-D", "-U", *TARGET)

# Sources clang cannot parse yet, each with the reason. Remove an entry once
# the clang-tidy CI pins has caught up.
SKIPPED = {
    # Its static_asserts evaluate std::sin and std::sqrt, constexpr since
    # C++26 (P1383R2), which clang does not implement yet:
    # https://github.com/llvm/llvm-project/issues/105385. The headers it
    # instantiates are checked on their own (--headers).
    Path(__file__).resolve().parent / "utility_sanity.cpp",
}


def arguments(entry):
    """The compiler and its arguments for one compile_commands entry."""
    if "arguments" in entry:
        return entry["arguments"]
    return shlex.split(entry["command"], posix=False)


def kept(args):
    """The options of @p args that clang-tidy needs, in order. Include
    directories are made absolute and normal: the header filters of the
    .clang-tidy files match the paths headers are found under, and
    tests/qemu/../../task.hpp is no test header."""
    out = []
    pending = None
    for arg in args[1:]:
        if pending:
            out.append(str(Path(arg).resolve()) if pending in ("-I", "-isystem") else arg)
            pending = None
        elif arg in ("-I", "-isystem", "-D", "-U"):
            out.append(arg)
            pending = arg
        elif arg.startswith("-I"):
            out += ["-I", str(Path(arg[2:]).resolve())]
        elif arg.startswith(KEPT):
            out.append(arg)
    return out


def compiler_view(compiler, options):
    """The target triple and the system include directories of @p compiler
    under @p options."""
    flags = [o for o in options if o.startswith(TARGET)]
    compiler = compiler.strip('"')
    triple = subprocess.run([compiler, *flags, "-dumpmachine"],
                            capture_output=True, text=True, check=True).stdout.strip()
    result = subprocess.run([compiler, *flags, "-xc++", "-E", "-v", "-"],
                            input="", capture_output=True, text=True, check=True)
    lines = result.stderr.splitlines()
    start = lines.index("#include <...> search starts here:") + 1
    end = lines.index("End of search list.")
    return triple, [str(Path(line.strip()).resolve()) for line in lines[start:end]]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--clang-tidy", required=True)
    parser.add_argument("--build", required=True, type=Path, action="append")
    parser.add_argument("--headers", type=Path, action="append", default=[],
                        help="a directory whose .hpp files are each checked on their own, "
                             "with every set of options of the first build")
    parser.add_argument("--checks", help="checks to run instead of the .clang-tidy files' (clang-tidy --checks)")
    parser.add_argument("--fix", action="store_true",
                        help="apply the fixes clang-tidy offers, once each, with clang-apply-replacements")
    parser.add_argument("--jobs", type=int, default=None)
    parser.add_argument("files", nargs="*", type=Path)
    options = parser.parse_args()

    wanted = {f.resolve() for f in options.files}
    seen = set()
    units = []
    views = {}
    first_build = []
    for build in options.build:
        for entry in json.loads((build / "compile_commands.json").read_text()):
            source = Path(entry["file"]).resolve()
            args = arguments(entry)
            flags = kept(args)
            seen_as = tuple(sorted(a for a in flags if not a.startswith(TARGET)))
            key = (args[0], tuple(f for f in flags if f.startswith(TARGET)))
            if build == options.build[0]:
                first_build.append((seen_as, key, args[0], flags))
            if source in SKIPPED or (wanted and source not in wanted) or (source, seen_as) in seen:
                continue
            seen.add((source, seen_as))
            if key not in views:
                views[key] = compiler_view(args[0], flags)
            units.append((source, flags, *views[key]))

    # A header is checked on its own under the .clang-tidy of its directory: a
    # source file applies its own configuration to the headers it includes,
    # which is not theirs, and reports them only within its own scope.
    headers = sorted(h.resolve() for d in options.headers for h in d.glob("*.hpp"))
    header_sets = {}
    for seen_as, key, compiler, flags in first_build:
        header_sets.setdefault(seen_as, (key, compiler, flags))
    for header in headers:
        if wanted and header not in wanted:
            continue
        for key, compiler, flags in header_sets.values():
            if key not in views:
                views[key] = compiler_view(compiler, flags)
            units.append((header, ["-xc++-header", *flags], *views[key]))

    if not units:
        sys.exit("no source to check")

    # A header is seen by several units: each exports its fixes, and
    # clang-apply-replacements applies every distinct one once.
    fixes = Path(tempfile.mkdtemp(prefix="tidy-fixes-")) if options.fix else None

    def check(numbered):
        number, (source, flags, triple, includes) = numbered
        extra = [f"--target={triple}", "-nostdinc", *flags]
        for directory in includes:
            extra += ["-isystem", directory]
        command = [options.clang_tidy, "--quiet"]
        if options.checks:
            command.append(f"--checks={options.checks}")
        if fixes:
            command.append(f"--export-fixes={fixes / f'{number}.yaml'}")
        command += [str(source), "--", *extra]
        result = subprocess.run(command, capture_output=True, text=True)
        return source, result.returncode, result.stdout + result.stderr

    failed = 0
    with concurrent.futures.ThreadPoolExecutor(options.jobs) as pool:
        for _, code, output in pool.map(check, enumerate(units)):
            report = "\n".join(line for line in output.splitlines()
                               if "warnings generated" not in line and "warning generated" not in line)
            if code != 0 or "warning:" in report or "error:" in report:
                failed += 1
                print(report.strip())
    print(f"{len(units)} files checked, {failed} with findings")
    if fixes:
        tidy = Path(options.clang_tidy)
        apply = tidy.with_name("clang-apply-replacements" + tidy.suffix)
        subprocess.run([str(apply), str(fixes)], check=True)
        shutil.rmtree(fixes)
        print("fixes applied")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
