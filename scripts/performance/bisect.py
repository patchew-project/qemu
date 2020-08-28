#!/usr/bin/env python3

"""
Locate the commit that caused a performance degradation or improvement in
QEMU using the git bisect command (binary search).

This file is a part of the project "TCG Continuous Benchmarking".

Copyright (C) 2020  Ahmed Karaman <ahmedkhaledkaraman@gmail.com>
Copyright (C) 2020  Aleksandar Markovic <aleksandar.qemu.devel@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
"""

import argparse
import multiprocessing
import tempfile
import os
import shutil
import subprocess
import sys

from typing import List


# --------------------------- GIT WRAPPERS --------------------------
def git_bisect(qemu_path: str, qemu_build_path: str, command: str,
               args: List[str] = None) -> str:
    """
    Wrapper function for running git bisect.

    Parameters:
    qemu_path (str): QEMU path
    qemu_build_path (str): Path to the build directory with configuration files
    command (str): bisect command (start|fast|slow|reset)
    args (list): Optional arguments

    Returns:
    (str): git bisect stdout.
    """
    process = ["git", "bisect", command]
    if args:
        process += args
    bisect = subprocess.run(process,
                            cwd=qemu_path,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            check=False)
    if bisect.returncode:
        clean_exit(qemu_build_path, bisect.stderr.decode("utf-8"))

    return bisect.stdout.decode("utf-8")


def git_checkout(commit: str, qemu_path: str, qemu_build_path: str) -> None:
    """
    Wrapper function for checking out a given git commit.

    Parameters:
    commit (str): Commit hash of a git commit
    qemu_path (str): QEMU path
    qemu_build_path (str): Path to the build directory with configuration files
    """
    checkout_commit = subprocess.run(["git",
                                      "checkout",
                                      commit],
                                     cwd=qemu_path,
                                     stdout=subprocess.DEVNULL,
                                     stderr=subprocess.PIPE,
                                     check=False)
    if checkout_commit.returncode:
        clean_exit(qemu_build_path, checkout_commit.stderr.decode("utf-8"))


def git_clone(qemu_path: str) -> None:
    """
    Wrapper function for cloning QEMU git repo from GitHub.

    Parameters:
    qemu_path (str): Path to clone the QEMU repo to
    """
    clone_qemu = subprocess.run(["git",
                                 "clone",
                                 "https://github.com/qemu/qemu.git",
                                 qemu_path],
                                stderr=subprocess.STDOUT,
                                check=False)
    if clone_qemu.returncode:
        sys.exit("Failed to clone QEMU!")
# -------------------------------------------------------------------


def check_requirements(tool: str) -> None:
    """
    Verify that all script requirements are installed (perf|callgrind & git).

    Parameters:
    tool (str): Tool used for the measurement (perf or callgrind)
    """
    if tool == "perf":
        check_perf_installation = subprocess.run(["which", "perf"],
                                                 stdout=subprocess.DEVNULL,
                                                 check=False)
        if check_perf_installation.returncode:
            sys.exit("Please install perf before running the script.")

        # Insure user has previllage to run perf
        check_perf_executability = subprocess.run(["perf", "stat", "ls", "/"],
                                                  stdout=subprocess.DEVNULL,
                                                  stderr=subprocess.DEVNULL,
                                                  check=False)
        if check_perf_executability.returncode:
            sys.exit("""
        Error:
        You may not have permission to collect stats.
        Consider tweaking /proc/sys/kernel/perf_event_paranoid,
        which controls use of the performance events system by
        unprivileged users (without CAP_SYS_ADMIN).
        -1: Allow use of (almost) all events by all users
            Ignore mlock limit after perf_event_mlock_kb without CAP_IPC_LOCK
        0: Disallow ftrace function tracepoint by users without CAP_SYS_ADMIN
            Disallow raw tracepoint access by users without CAP_SYS_ADMIN
        1: Disallow CPU event access by users without CAP_SYS_ADMIN
        2: Disallow kernel profiling by users without CAP_SYS_ADMIN
        To make this setting permanent, edit /etc/sysctl.conf too, e.g.:
        kernel.perf_event_paranoid = -1

        *Alternatively, you can run this script under sudo privileges.
        """)
    elif tool == "callgrind":
        check_valgrind_installation = subprocess.run(["which", "valgrind"],
                                                     stdout=subprocess.DEVNULL,
                                                     check=False)
        if check_valgrind_installation.returncode:
            sys.exit("Please install valgrind before running the script.")

    # Insure that git is installed
    check_git_installation = subprocess.run(["which", "git"],
                                            stdout=subprocess.DEVNULL,
                                            check=False)
    if check_git_installation.returncode:
        sys.exit("Please install git before running the script.")


def clean_exit(qemu_build_path: str, error_message: str) -> None:
    """
    Clean up intermediate files and exit.

    Parameters:
    qemu_build_path (str): Path to the build directory with configuration files
    error_message (str): Error message to display after exiting
    """
    shutil.rmtree(qemu_build_path)
    sys.exit(error_message)


def make(qemu_build_path: str) -> None:
    """
    Build QEMU by running the Makefile.

    Parameters:
    qemu_build_path (str): Path to the build directory with configuration files
    """
    run_make = subprocess.run(["make",
                               "-j",
                               str(multiprocessing.cpu_count())],
                              cwd=qemu_build_path,
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.PIPE,
                              check=False)
    if run_make.returncode:
        clean_exit(qemu_build_path, run_make.stderr.decode("utf-8"))


def measure_instructions(tool: str, qemu_build_path: str, target: str,
                         command: List[str]) -> int:
    """
    Measure the number of instructions when running an program with QEMU.

    Parameters:
    tool (str): Tool used for the measurement (perf|callgrind)
    qemu_build_path (str): Path to the build directory with configuration files
    target (str): QEMU target
    command (list): Program path and arguments

    Returns:
    (int): Number of instructions.
    """
    qemu_exe_path = os.path.join(qemu_build_path,
                                 "{}-linux-user".format(target),
                                 "qemu-{}".format(target))
    instructions = 0
    if tool == "perf":
        run_perf = subprocess.run((["perf",
                                    "stat",
                                    "-x",
                                    " ",
                                    "-e",
                                    "instructions",
                                    qemu_exe_path]
                                   + command),
                                  stdout=subprocess.DEVNULL,
                                  stderr=subprocess.PIPE,
                                  check=False)
        if run_perf.returncode:
            clean_exit(qemu_build_path, run_perf.stderr.decode("utf-8"))

        else:
            perf_output = run_perf.stderr.decode("utf-8").split(" ")
            instructions = int(perf_output[0])

    elif tool == "callgrind":
        with tempfile.NamedTemporaryFile() as tmpfile:
            run_callgrind = subprocess.run((["valgrind",
                                             "--tool=callgrind",
                                             "--callgrind-out-file={}".format(
                                                 tmpfile.name),
                                             qemu_exe_path]
                                            + command),
                                           stdout=subprocess.DEVNULL,
                                           stderr=subprocess.PIPE,
                                           check=False)
        if run_callgrind.returncode:
            clean_exit(qemu_build_path, run_callgrind.stderr.decode("utf-8"))

        else:
            callgrind_output = run_callgrind.stderr.decode("utf-8").split("\n")
            instructions = int(callgrind_output[8].split(" ")[-1])

    return instructions


def main():
    """
    Parse the command line arguments then start the execution.

    Syntax:
        bisect.py [-h] -s,--start START [-e,--end END] [-q,--qemu QEMU] \
        --target TARGET --tool {perf,callgrind} -- \
        <target executable> [<target executable options>]

    Arguments:
        [-h] - Print the script arguments help message
        -s,--start START - First commit hash in the search range
        [-e,--end END] - Last commit hash in the search range
                (default: Latest commit)
        [-q,--qemu QEMU] - QEMU path.
                    (default: Path to a GitHub QEMU clone)
        --target TARGET - QEMU target name
        --tool {perf,callgrind} - Underlying tool used for measurements

    Example of usage:
        bisect.py --start=fdd76fecdd --qemu=/path/to/qemu --target=ppc \
        --tool=perf coulomb_double-ppc -n 1000
    """

    # Parse the command line arguments
    parser = argparse.ArgumentParser(
        usage="bisect.py [-h] -s,--start START [-e,--end END] [-q,--qemu QEMU]"
        " --target TARGET --tool {perf,callgrind} -- "
        "<target executable> [<target executable options>]")

    parser.add_argument("-s", "--start", dest="start", type=str, required=True,
                        help="First commit hash in the search range")
    parser.add_argument("-e", "--end", dest="end", type=str, default="master",
                        help="Last commit hash in the search range")
    parser.add_argument("-q", "--qemu", dest="qemu", type=str, default="",
                        help="QEMU path")
    parser.add_argument("--target", dest="target", type=str, required=True,
                        help="QEMU target")
    parser.add_argument("--tool", dest="tool", choices=["perf", "callgrind"],
                        required=True, help="Tool used for measurements")

    parser.add_argument("command", type=str, nargs="+", help=argparse.SUPPRESS)

    args = parser.parse_args()

    # Extract the needed variables from the args
    start_commit = args.start
    end_commit = args.end
    qemu = args.qemu
    target = args.target
    tool = args.tool
    command = args.command

    # Set QEMU path
    if qemu == "":
        # Create a temp directory for cloning QEMU
        tmpdir = tempfile.TemporaryDirectory()
        qemu_path = os.path.join(tmpdir.name, "qemu")

        # Clone QEMU into the temporary directory
        print("Fetching QEMU: ", end="", flush=True)
        git_clone(qemu_path)
        print()
    else:
        qemu_path = qemu

    # Create the build directory
    qemu_build_path = os.path.join(qemu_path, "tmp-build-gcc")

    if not os.path.exists(qemu_build_path):
        os.mkdir(qemu_build_path)
    else:
        sys.exit("A build directory with the same name (tmp-build-gcc) used in"
                 " the script is already in the provided QEMU path.")

    # Configure QEMU
    configure = subprocess.run(["../configure",
                                "--target-list={}-linux-user".format(target)],
                               cwd=qemu_build_path,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE,
                               check=False)
    if configure.returncode:
        clean_exit(qemu_build_path, configure.stderr.decode("utf-8"))

    # Do performance measurements for the start commit
    git_checkout(start_commit, qemu_path, qemu_build_path)
    make(qemu_build_path)
    start_commit_instructions = measure_instructions(tool,
                                                     qemu_build_path,
                                                     target,
                                                     command)
    print("{:<30} {}".format("Start Commit Instructions:",
                             format(start_commit_instructions, ",")))

    # Do performance measurements for the end commit
    git_checkout(end_commit, qemu_path, qemu_build_path)
    make(qemu_build_path)
    end_commit_instructions = measure_instructions(tool,
                                                   qemu_build_path,
                                                   target,
                                                   command)
    print("{:<30} {}".format("End Commit Instructions:",
                             format(end_commit_instructions, ",")))

    # Calculate performance difference between start and end commits
    performance_difference = \
        (start_commit_instructions - end_commit_instructions) / \
        max(end_commit_instructions, start_commit_instructions) * 100
    performance_change = "+" if performance_difference > 0 else "-"
    print("{:<30} {}".format("Performance Change:",
                             performance_change +
                             str(round(abs(performance_difference), 3))+"%"))

    # Set the custom terms used for progressing in "git bisect"
    term_old = "fast" if performance_difference < 0 else "slow"
    term_new = "slow" if term_old == "fast" else "fast"

    # Start git bisect
    git_bisect(qemu_path, qemu_build_path, "start",
               ["--term-old", term_old, "--term-new", term_new])
    # Set start commit state
    git_bisect(qemu_path, qemu_build_path, term_old, [start_commit])
    # Set end commit state
    bisect_output = git_bisect(
        qemu_path, qemu_build_path, term_new, [end_commit])
    # Print estimated bisect steps
    print("\n{:<30} {}\n".format(
        "Estimated Number of Steps:", bisect_output.split()[9]))

    # Initialize bisect_count to track the number of performed
    bisect_count = 1

    while True:
        print("**************BISECT STEP {}**************".
              format(bisect_count))

        make(qemu_build_path)

        instructions = measure_instructions(tool,
                                            qemu_build_path,
                                            target,
                                            command)
        # Find the difference between the current instructions and start/end
        # instructions.
        diff_end = abs(instructions - end_commit_instructions)
        diff_start = abs(instructions - start_commit_instructions)

        # If current number of insructions is closer to that of start,
        # set current commit as term_old.
        # Else, set current commit as term_new.
        if diff_end > diff_start:
            bisect_command = term_old
        else:
            bisect_command = term_new

        print("{:<20} {}".format("Instructions:", format(instructions, ",")))
        print("{:<20} {}".format("Status:", "{} commit".
                                 format(bisect_command)))

        bisect_output = git_bisect(qemu_path, qemu_build_path, bisect_command)

        # Continue if still bisecting,
        # else, print result and break.
        if not bisect_output.split(" ")[0] == "Bisecting:":
            print("\n*****************BISECT RESULT*****************")
            commit_message_start = bisect_output.find("commit\n") + 7
            commit_message_end = bisect_output.find(":040000") - 1
            print(bisect_output[commit_message_start:commit_message_end])
            break

        bisect_count += 1

    # Reset git bisect
    git_bisect(qemu_path, qemu_build_path, "reset")

    # Delete temp build directory
    shutil.rmtree(qemu_build_path)


if __name__ == "__main__":
    main()
