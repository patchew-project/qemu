#!/usr/bin/env python3
# ---------------------------------------------------------------------------- #

from configparser import ConfigParser
from contextlib import contextmanager
from dataclasses import dataclass
import json
import os
import os.path
import shlex
import subprocess
import sys
import re
from argparse import (
    Action,
    ArgumentParser,
    Namespace,
    RawDescriptionHelpFormatter,
)
from multiprocessing import Pool
from pathlib import Path
from tempfile import TemporaryDirectory
import textwrap
import time
from traceback import print_exc
from typing import (
    Callable,
    Iterable,
    Iterator,
    List,
    Mapping,
    NoReturn,
    Optional,
    Sequence,
    Union,
)

import clang.cindex  # type: ignore

from static_analyzer import CHECKS, CheckContext

# ---------------------------------------------------------------------------- #
# Usage


def parse_args() -> Namespace:

    available_checks = "\n".join(
        f"  {name}   {' '.join((CHECKS[name].checker.__doc__ or '').split())}"
        for name in sorted(CHECKS)
    )

    parser = ArgumentParser(
        allow_abbrev=False,
        formatter_class=RawDescriptionHelpFormatter,
        description=textwrap.dedent(
            """
            Checks are best-effort, but should never report false positives.

            This only considers translation units enabled under the given QEMU
            build configuration. Note that a single .c file may give rise to
            several translation units.

            You should build QEMU before running this, since some translation
            units depend on files that are generated during the build. If you
            don't, you'll get errors, but should never get false negatives.
            """
        ),
        epilog=textwrap.dedent(
            f"""
            available checks:
            {available_checks}

            exit codes:
            0  No problems found.
            1  Internal failure.
            2  Bad usage.
            3  Problems found in one or more translation units.
            """
        ),
    )

    parser.add_argument(
        "build_dir",
        type=Path,
        help="Path to the build directory.",
    )

    parser.add_argument(
        "translation_units",
        type=Path,
        nargs="*",
        help=(
            "Analyze only translation units whose root source file matches or"
            " is under one of the given paths."
        ),
    )

    # add regular options

    parser.add_argument(
        "-c",
        "--check",
        metavar="CHECK",
        dest="check_names",
        choices=sorted(CHECKS),
        action="append",
        help=(
            "Enable the given check. Can be given multiple times. If not given,"
            " all checks are enabled."
        ),
    )

    parser.add_argument(
        "-j",
        "--jobs",
        dest="threads",
        type=int,
        default=os.cpu_count() or 1,
        help=(
            f"Number of threads to employ. Defaults to {os.cpu_count() or 1} on"
            f" this machine."
        ),
    )

    # add development options

    dev_options = parser.add_argument_group("development options")

    dev_options.add_argument(
        "--profile",
        metavar="SORT_KEY",
        help=(
            "Profile execution. Forces single-threaded execution. The argument"
            " specifies how to sort the results; see"
            " https://docs.python.org/3/library/profile.html#pstats.Stats.sort_stats"
        ),
    )

    dev_options.add_argument(
        "--skip-checks",
        action="store_true",
        help="Do everything except actually running the checks.",
    )

    dev_options.add_argument(
        "--test",
        action=TestAction,
        nargs=0,
        help="Run tests for all checks and exit.",
    )

    # parse arguments

    try:
        args = parser.parse_args()
    except TestSentinelError:
        # --test was specified
        if len(sys.argv) > 2:
            parser.error("--test must be given alone")
        return Namespace(test=True)

    args.check_names = sorted(set(args.check_names or CHECKS))

    return args


class TestAction(Action):
    def __call__(self, parser, namespace, values, option_string=None):
        raise TestSentinelError


class TestSentinelError(Exception):
    pass


# ---------------------------------------------------------------------------- #
# Main


def main() -> int:

    args = parse_args()

    if args.test:

        return test()

    elif args.profile:

        import cProfile
        import pstats

        profile = cProfile.Profile()

        try:
            return profile.runcall(lambda: analyze(args))
        finally:
            stats = pstats.Stats(profile, stream=sys.stderr)
            stats.strip_dirs()
            stats.sort_stats(args.profile)
            stats.print_stats()

    else:

        return analyze(args)


def test() -> int:

    for (check_name, check) in CHECKS.items():
        for group in check.test_groups:
            for input in group.inputs:
                run_test(
                    check_name, group.location, input, group.expected_output
                )

    return 0


def analyze(args: Namespace) -> int:

    tr_units = get_translation_units(args)

    # analyze translation units

    start_time = time.monotonic()
    results: List[bool] = []

    if len(tr_units) == 1:
        progress_suffix = " of 1 translation unit...\033[0m\r"
    else:
        progress_suffix = f" of {len(tr_units)} translation units...\033[0m\r"

    def print_progress() -> None:
        print(f"\033[0;34mAnalyzed {len(results)}" + progress_suffix, end="")

    print_progress()

    def collect_results(results_iter: Iterable[bool]) -> None:
        if sys.stdout.isatty():
            for r in results_iter:
                results.append(r)
                print_progress()
        else:
            for r in results_iter:
                results.append(r)

    if tr_units:

        if args.threads == 1:

            collect_results(map(analyze_translation_unit, tr_units))

        else:

            # Mimic Python's default pool.map() chunk size, but limit it to
            # 5 to avoid very big chunks when analyzing thousands of
            # translation units.
            chunk_size = min(5, -(-len(tr_units) // (args.threads * 4)))

            with Pool(processes=args.threads) as pool:
                collect_results(
                    pool.imap_unordered(
                        analyze_translation_unit, tr_units, chunk_size
                    )
                )

    end_time = time.monotonic()

    # print summary

    if len(tr_units) == 1:
        message = "Analyzed 1 translation unit"
    else:
        message = f"Analyzed {len(tr_units)} translation units"

    message += f" in {end_time - start_time:.1f} seconds."

    print(f"\033[0;34m{message}\033[0m")

    # exit

    return 0 if all(results) else 3


# ---------------------------------------------------------------------------- #
# Translation units


@dataclass
class TranslationUnit:
    absolute_path: str
    build_working_dir: str
    build_command: str
    system_include_paths: Sequence[str]
    check_names: Sequence[str]
    custom_printer: Optional[Callable[[str], None]]


def get_translation_units(args: Namespace) -> Sequence["TranslationUnit"]:
    """Return a list of translation units to be analyzed."""

    system_include_paths = get_clang_system_include_paths()
    compile_commands = load_compilation_database(args.build_dir)

    # get all translation units

    tr_units: Iterable[TranslationUnit] = (
        TranslationUnit(
            absolute_path=str(Path(cmd["directory"], cmd["file"]).resolve()),
            build_working_dir=cmd["directory"],
            build_command=cmd["command"],
            system_include_paths=system_include_paths,
            check_names=args.check_names,
            custom_printer=None,
        )
        for cmd in compile_commands
    )

    # ignore translation units from git submodules

    repo_root = (args.build_dir / "Makefile").resolve(strict=True).parent
    module_file = repo_root / ".gitmodules"
    assert module_file.exists()

    modules = ConfigParser()
    modules.read(module_file)

    disallowed_prefixes = [
        # ensure path is slash-terminated
        os.path.join(repo_root, section["path"], "")
        for section in modules.values()
        if "path" in section
    ]

    tr_units = (
        ctx
        for ctx in tr_units
        if all(
            not ctx.absolute_path.startswith(prefix)
            for prefix in disallowed_prefixes
        )
    )

    # filter translation units by command line arguments

    if args.translation_units:

        allowed_prefixes = [
            # ensure path exists and is slash-terminated (even if it is a file)
            os.path.join(path.resolve(strict=True), "")
            for path in args.translation_units
        ]

        tr_units = (
            ctx
            for ctx in tr_units
            if any(
                (ctx.absolute_path + "/").startswith(prefix)
                for prefix in allowed_prefixes
            )
        )

    # ensure that at least one translation unit is selected

    tr_unit_list = list(tr_units)

    if not tr_unit_list:
        fatal("No translation units to analyze")

    # disable all checks if --skip-checks was given

    if args.skip_checks:
        for context in tr_unit_list:
            context.check_names = []

    return tr_unit_list


def get_clang_system_include_paths() -> Sequence[str]:

    # libclang does not automatically include clang's standard system include
    # paths, so we ask clang what they are and include them ourselves.

    result = subprocess.run(
        ["clang", "-E", "-", "-v"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        universal_newlines=True,  # decode output using default encoding
        check=True,
    )

    # Module `re` does not support repeated group captures.
    pattern = (
        r"#include <...> search starts here:\n"
        r"((?: \S*\n)+)"
        r"End of search list."
    )

    match = re.search(pattern, result.stderr, re.MULTILINE)
    assert match is not None

    return [line[1:] for line in match.group(1).splitlines()]


def load_compilation_database(build_dir: Path) -> Sequence[Mapping[str, str]]:

    # clang.cindex.CompilationDatabase.getCompileCommands() apparently produces
    # entries for files not listed in compile_commands.json in a best-effort
    # manner, which we don't want, so we parse the JSON ourselves instead.

    path = build_dir / "compile_commands.json"

    try:
        with path.open("r") as f:
            return json.load(f)
    except FileNotFoundError:
        fatal(f"{path} does not exist")


# ---------------------------------------------------------------------------- #
# Analysis


def analyze_translation_unit(tr_unit: TranslationUnit) -> bool:

    check_context = get_check_context(tr_unit)

    try:
        for name in tr_unit.check_names:
            CHECKS[name].checker(check_context)
    except Exception as e:
        raise RuntimeError(f"Error analyzing {check_context._rel_path}") from e

    return not check_context._problems_found


def get_check_context(tr_unit: TranslationUnit) -> CheckContext:

    # relative to script's original working directory
    rel_path = os.path.relpath(tr_unit.absolute_path)

    # load translation unit

    command = shlex.split(tr_unit.build_command)

    adjusted_command = [
        # keep the original compilation command name
        command[0],
        # ignore unknown GCC warning options
        "-Wno-unknown-warning-option",
        # keep all other arguments but the last, which is the file name
        *command[1:-1],
        # add clang system include paths
        *(
            arg
            for path in tr_unit.system_include_paths
            for arg in ("-isystem", path)
        ),
        # replace relative path to get absolute location information
        tr_unit.absolute_path,
    ]

    # clang can warn about things that GCC doesn't
    if "-Werror" in adjusted_command:
        adjusted_command.remove("-Werror")

    # We change directory for options like -I to work, but have to change back
    # to have correct relative paths in messages.
    with cwd(tr_unit.build_working_dir):

        try:
            tu = clang.cindex.TranslationUnit.from_source(
                filename=None, args=adjusted_command
            )
        except clang.cindex.TranslationUnitLoadError as e:
            raise RuntimeError(f"Failed to load {rel_path}") from e

    if tr_unit.custom_printer is not None:
        printer = tr_unit.custom_printer
    elif sys.stdout.isatty():
        # add padding to fully overwrite progress message
        printer = lambda s: print(s.ljust(50))
    else:
        printer = print

    check_context = CheckContext(
        translation_unit=tu,
        translation_unit_path=tr_unit.absolute_path,
        _rel_path=rel_path,
        _build_working_dir=Path(tr_unit.build_working_dir),
        _problems_found=False,
        _printer=printer,
    )

    # check for error/fatal diagnostics

    for diag in tu.diagnostics:
        if diag.severity >= clang.cindex.Diagnostic.Error:
            check_context._problems_found = True
            location = check_context.format_location(diag)
            check_context._printer(
                f"\033[0;33m{location}: {diag.spelling}; this may lead to false"
                f" positives and negatives\033[0m"
            )

    return check_context


# ---------------------------------------------------------------------------- #
# Tests


def run_test(
    check_name: str, location: str, input: str, expected_output: str
) -> None:

    with TemporaryDirectory() as temp_dir:

        os.chdir(temp_dir)

        input_path = Path(temp_dir) / "file.c"
        input_path.write_text(input)

        actual_output_list = []

        tu_context = TranslationUnit(
            absolute_path=str(input_path),
            build_working_dir=str(input_path.parent),
            build_command=f"cc {shlex.quote(str(input_path))}",
            system_include_paths=[],
            check_names=[check_name],
            custom_printer=lambda s: actual_output_list.append(s + "\n"),
        )

        check_context = get_check_context(tu_context)

        # analyze translation unit

        try:

            for name in tu_context.check_names:
                CHECKS[name].checker(check_context)

        except Exception:

            print(
                f"\033[0;31mTest defined at {location} raised an"
                f" exception.\033[0m"
            )
            print(f"\033[0;31mInput:\033[0m")
            print(input, end="")
            print(f"\033[0;31mExpected output:\033[0m")
            print(expected_output, end="")
            print(f"\033[0;31mException:\033[0m")
            print_exc(file=sys.stdout)
            print(f"\033[0;31mAST:\033[0m")
            check_context.print_tree(check_context.translation_unit.cursor)

            sys.exit(1)

        actual_output = "".join(actual_output_list)

        if actual_output != expected_output:

            print(f"\033[0;33mTest defined at {location} failed.\033[0m")
            print(f"\033[0;33mInput:\033[0m")
            print(input, end="")
            print(f"\033[0;33mExpected output:\033[0m")
            print(expected_output, end="")
            print(f"\033[0;33mActual output:\033[0m")
            print(actual_output, end="")
            print(f"\033[0;33mAST:\033[0m")
            check_context.print_tree(check_context.translation_unit.cursor)

            sys.exit(3)


# ---------------------------------------------------------------------------- #
# Utilities


@contextmanager
def cwd(path: Union[str, Path]) -> Iterator[None]:

    original_cwd = os.getcwd()
    os.chdir(path)

    try:
        yield
    finally:
        os.chdir(original_cwd)


def fatal(message: str) -> NoReturn:
    print(f"\033[0;31mERROR: {message}\033[0m")
    sys.exit(1)


# ---------------------------------------------------------------------------- #

if __name__ == "__main__":
    sys.exit(main())

# ---------------------------------------------------------------------------- #
