#!/usr/bin/env python3

"""
Core script for performing nightly performance tests on QEMU.

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
import csv
import datetime
import glob
import multiprocessing
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Dict, List, Optional, Union


def get_benchmark_name(benchmark_path: str) -> str:
    """
    Return the benchmark name given its path.

    Parameters:
    benchmarks_path (str): Absolute path to benchmark

    Return:
    (str): Benchmark name
    """
    benchmark_source_file = os.path.split(benchmark_path)[1]
    return os.path.splitext(benchmark_source_file)[0]


def get_benchmark_parent_dir(benchmark_path: str) -> str:
    """
    Return the benchmark parent directory name given the benchmark path.

    Parameters:
    benchmarks_path (str): Absolute path to benchmark

    Return:
    (str): Benchmark parent directory name
    """
    benchmark_parent_dir_path = os.path.split(benchmark_path)[0]
    benchmark_parent_dir = os.path.split(benchmark_parent_dir_path)[1]

    return benchmark_parent_dir


def get_executable_parent_dir_path(
        benchmark_path: str, benchmarks_executables_dir_path: str) -> str:
    """
    Return the executables parent directory of a benchmark given its path.
    This is the directory that includes all compiled executables for the
    benchmark.

    Parameters:
    benchmarks_path (str): Absolute path to benchmark
    benchmarks_executables_dir_path (str): Absolute path to the executables

    Return:
    (str): Executables parent directory path
    """
    benchmark_parent_dir_path = os.path.split(benchmark_path)[0]
    benchmark_parent_dir = os.path.split(benchmark_parent_dir_path)[1]
    executable_parent_dir_path = os.path.join(benchmarks_executables_dir_path,
                                              benchmark_parent_dir)

    return executable_parent_dir_path


def get_commit_hash(commit_tag: str, qemu_path: str) -> str:
    """
    Find commit hash given the Git commit tag.

    Parameters:
    commit_tag (str): Commit tag
    qemu_path (str): Absolute path to QEMU

    Returns:
    (str): 8 digit commit hash
    """

    commit_hash = subprocess.run(["git",
                                  "rev-parse",
                                  commit_tag],
                                 cwd=qemu_path,
                                 stdout=subprocess.PIPE,
                                 check=False)
    if commit_hash.returncode:
        clean_exit(qemu_path,
                   "Failed to find the commit hash of {}.".format(commit_tag))

    return commit_hash.stdout.decode("utf-8")[:8]


def git_checkout(commit: str, qemu_path: str) -> None:
    """
    Checkout a given Git commit.
    Also pull the latest changes from origin/master if the commit is "master".

    Parameters:
    commit (str): Commit hash or tag
    qemu_path (str): Absolute path to QEMU
    """
    print(datetime.datetime.utcnow().isoformat(),
          "- Checking out {}".format(commit), file=sys.stderr, flush=True)

    checkout_commit = subprocess.run(["git",
                                      "checkout",
                                      commit],
                                     cwd=qemu_path,
                                     stdout=subprocess.DEVNULL,
                                     stderr=subprocess.PIPE,
                                     check=False)
    if checkout_commit.returncode:
        clean_exit(qemu_path, checkout_commit.stderr.decode("utf-8"))

    if commit == "master":
        print(datetime.datetime.utcnow().isoformat(),
              "- Pulling the latest changes from QEMU master",
              file=sys.stderr, flush=True)
        # Try pulling the latest changes.
        # Limit the number of failed trials to 10.
        failure_count, failure_limit = 0, 10
        while True:
            pull_latest = subprocess.run(["git",
                                          "pull",
                                          "origin",
                                          "master"],
                                         cwd=qemu_path,
                                         stdout=subprocess.DEVNULL,
                                         check=False)
            if pull_latest.returncode:
                failure_count += 1
                if failure_count == failure_limit:
                    print(datetime.datetime.utcnow().isoformat(),
                          "- Trial {}/{}: Failed to pull QEMU".format(
                              failure_count, failure_limit),
                          file=sys.stderr, flush=True)
                    clean_exit(qemu_path, "")
                else:
                    print(datetime.datetime.utcnow().isoformat(),
                          "- Trial {}/{}: Failed to pull QEMU"
                          " ... retrying again in a minute!".format(
                              failure_count, failure_limit),
                          file=sys.stderr, flush=True)
                    time.sleep(60)
            else:
                break


def git_clone(qemu_path: str) -> None:
    """
    Clone QEMU from Git.

    Parameters:
    qemu_path (str): Absolute path to clone the QEMU repo to
    """
    # Try cloning QEMU.
    # Limit the number of failed trials to 10.
    failure_count, failure_limit = 0, 10
    while True:
        clone_qemu = subprocess.run(["git",
                                     "clone",
                                     "https://git.qemu.org/git/qemu.git",
                                     qemu_path],
                                    check=False)
        if clone_qemu.returncode:
            failure_count += 1
            if failure_count == failure_limit:
                print(datetime.datetime.utcnow().isoformat(),
                      "- Trial {}/{}: Failed to clone QEMU".format(
                          failure_count, failure_limit),
                      file=sys.stderr, flush=True)
                clean_exit(qemu_path, "")
            else:
                print(datetime.datetime.utcnow().isoformat(),
                      "- Trial {}/{}: Failed to clone QEMU"
                      " ... retrying again in a minute!".format(
                          failure_count, failure_limit),
                      file=sys.stderr, flush=True)
                time.sleep(60)
        else:
            break


def build_qemu(qemu_path: str, git_tag: str, targets: List[str]) -> None:
    """
    Checkout the Git tag then configure and build QEMU.

    Parameters:
    qemu_path (str): Absolute path to QEMU
    git_tag (str): Git tag to checkout before building
    targets (List[str]): List of targets to configure QEMU for
    """

    # Clean the QEMU build path
    qemu_build_path = os.path.join(qemu_path, "build-gcc")
    if os.path.isdir(qemu_build_path):
        shutil.rmtree(qemu_build_path)
    os.mkdir(qemu_build_path)

    git_checkout(git_tag, qemu_path)

    # Specify target list for configuring QEMU
    target_list = ["{}-linux-user".format(target) for target in targets]

    # Configure QEMU
    print(datetime.datetime.utcnow().isoformat(),
          "- Running 'configure' for {}".format(git_tag),
          file=sys.stderr, flush=True)
    configure = subprocess.run(["../configure",
                                "--disable-system",
                                "--disable-tools",
                                "--target-list={}".
                                format(",".join(target_list))],
                               cwd=qemu_build_path,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE,
                               check=False)
    if configure.returncode:
        clean_exit(qemu_path, configure.stderr.decode("utf-8"))

    # Run "make -j$(nproc)"
    print(datetime.datetime.utcnow().isoformat(),
          "- Running 'make' for {}".format(git_tag), file=sys.stderr,
          flush=True)
    make = subprocess.run(["make",
                           "-j",
                           str(multiprocessing.cpu_count())],
                          cwd=qemu_build_path,
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE,
                          check=False)
    if make.returncode:
        clean_exit(qemu_path, make.stderr.decode("utf-8"))


def compile_target(benchmark_path: str, compiled_benchmark_path: str,
                   target_compiler: str) -> None:
    """
    Compile a benchmark using the provided cross compiler.

    Parameters:
    benchmarks_path (str): Absolute path to benchmark
    compiled_benchmark_path (str): Path to the output executable
    target_compiler (str): Cross compiler
    """
    compile_benchmark = subprocess.run([target_compiler,
                                        "-O2",
                                        "-static",
                                        "-w",
                                        benchmark_path,
                                        "-o",
                                        compiled_benchmark_path],
                                       check=False)
    if compile_benchmark.returncode:
        sys.exit("Compilation of {} failed".format(
            os.path.split(compiled_benchmark_path)[1]))


def measure_instructions(
        benchmark_path: str, benchmarks_executables_dir_path: str,
        qemu_path: str, targets: List[str]) -> List[List[Union[str, int]]]:
    """
    Measure the number of instructions when running an program with QEMU.

    Parameters:
    benchmarks_path (str): Absolute path to benchmark
    benchmarks_executables_dir_path (str): Absolute path to the executables
    qemu_path (str): Absolute path to QEMU
    targets (List[str]): List of QEMU targets

    Returns:
    (List[List[Union[str, int]]]): [[target_name, instructions],[...],...]
    """

    benchmark_name = get_benchmark_name(benchmark_path)
    executable_parent_dir_path = get_executable_parent_dir_path(
        benchmark_path, benchmarks_executables_dir_path)
    qemu_build_path = os.path.join(qemu_path, "build-gcc")

    instructions: List[List[Union[str, int]]] = []

    for target in targets:
        executable_path = os.path.join(
            executable_parent_dir_path, "{}-{}".format(benchmark_name, target))

        qemu_exe_path = os.path.join(qemu_build_path,
                                     "{}-linux-user".format(target),
                                     "qemu-{}".format(target))

        with tempfile.NamedTemporaryFile() as tmpfile:
            run_callgrind = subprocess.run(["valgrind",
                                            "--tool=callgrind",
                                            "--callgrind-out-file={}".format(
                                                tmpfile.name),
                                            qemu_exe_path,
                                            executable_path],
                                           stdout=subprocess.DEVNULL,
                                           stderr=subprocess.PIPE,
                                           check=False)
        if run_callgrind.returncode == 1:
            clean_exit(qemu_path, run_callgrind.stderr.decode("utf-8"))

        callgrind_output = run_callgrind.stderr.decode("utf-8").split("\n")
        instructions.append([target, int(callgrind_output[8].split(" ")[-1])])

    return instructions


def measure_master_instructions(
        reference_version_path: str, reference_commit_hash: str,
        latest_version_path: str, benchmark_path: str,
        benchmarks_executables_dir_path: str, qemu_path: str,
        targets: List[str]) -> List[List[Union[str, int]]]:
    """
    Measure the latest QEMU "master" instructions and also append the latest
    instructions and reference version instructions to the instructions list.

    Parameters:
    reference_version_path (str): Absolute path to reference version results
    reference_commit_hash (str): Git hash of the reference version
    latest_version_path (str): Absolute path to the latest version results
    benchmark_path (str): Absolute path to benchmark
    benchmarks_executables_dir_path (str):
                            Absolute path to the executables of the benchmark
    qemu_path (str): Absolute path to QEMU
    targets (List[str]): List of QEMU targets


    Return:
    (List[List[Union[str, int]]]):
            [[target_name, instructions, comparison_instructions],[...],...]
            comparsion_instructions: *[latest, reference]
            If latest is not available,
            then comparsion_instructions = reference
    """
    benchmark_name = get_benchmark_name(benchmark_path)

    print(datetime.datetime.utcnow().isoformat(),
          "- Measuring instructions for master - {}".format(benchmark_name),
          file=sys.stderr, flush=True)

    instructions = measure_instructions(
        benchmark_path, benchmarks_executables_dir_path, qemu_path, targets)

    reference_result = "{}-{}-results.csv".format(
        reference_commit_hash, benchmark_name)
    reference_result_path = os.path.join(
        reference_version_path, reference_result)

    # Find if this benchmark has a record in the latest results
    latest_result = ""
    latest_results = os.listdir(latest_version_path)
    for result in latest_results:
        if result.split("-")[1] == benchmark_name:
            latest_result = result
            break

    # Append instructions from latest version if available
    if latest_result != "":
        latest_result_path = os.path.join(latest_version_path, latest_result)
        with open(latest_result_path, "r") as file:
            file.readline()
            for target_instructions in instructions:
                target_instructions.append(
                    int(file.readline().split('"')[1].replace(",", "")))
        # Delete the latest results. The directory will contain the new latest
        # when the new "master" results are stored later.
        os.unlink(latest_result_path)

    # Append instructions from reference version
    with open(reference_result_path, "r") as file:
        file.readline()
        for target_instructions in instructions:
            target_instructions.append(
                int(file.readline().split('"')[1].replace(",", "")))

    return instructions


def calculate_percentage(old_instructions: int, new_instructions: int) -> str:
    """
    Calculate the change in percentage between two instruction counts

    Parameters:
    old_instructions (int): Old number
    new_instructions (int): New number

    Return:
    (str): [+|-][change][%] or "-----" in case of 0.01% change
    """
    percentage = round(((new_instructions - old_instructions) /
                        old_instructions) * 100, 3)
    return format_percentage(percentage)


def format_percentage(percentage: float) -> str:
    """
    Format the percentage value to add +|- and %.

    Parameters:
    percentage (float): Percentage

    Returns:
    (str): Formatted percentage string
    """
    if abs(percentage) <= 0.01:
        return "-----"
    return "+" + str(percentage) + "%" if percentage > 0 \
        else str(percentage) + "%"


def calculate_change(instructions: List[List[Union[str, int]]]) -> None:
    """
    Calculate the change in the recorded instructions for master compared to
    latest results and reference version results.

    Parameters:
    instructions (List[List[Union[str, int]]]):
            [[target_name, instructions, comparison_instructions],[...],...]
            comparsion_instructions: *[latest, reference]
            If latest is not available,
            then comparsion_instructions = reference
    """
    for target_instructions in instructions:
        target_instructions[-1] = calculate_percentage(
            int(target_instructions[-1]), int(target_instructions[1]))
        # If latest instructions exists
        if len(target_instructions) == 4:
            target_instructions[-2] = calculate_percentage(
                int(target_instructions[-2]), int(target_instructions[1]))


def calculate_average(results: List[List[List[Union[str, int]]]],
                      targets: List[str],
                      num_benchmarks: int) -> List[List[Union[str, int]]]:
    """
    Calculate the average results for each target for all benchmarks.

    Parameters:
    results (List[List[List[Union[str, int]]]]):
            [[target_name, instructions, comparison_instructions],[...],...]
            comparsion_instructions: *[latest, reference]
            If latest is not available,
            then comparsion_instructions = reference
    targets (List[str]): List of target names
    num_benchmarks (int): Number of benchmarks

    Return:
    (List[List[Union[str, int]]]):
            [[target_name, average_instructions, \
                comparison_instructions],[...],...]
            comparsion_instructions: *[average_latest, average_reference]
            If latest is not available,
            then comparsion_instructions = reference
    """
    average_instructions: List[List[Union[str, int]]] = []

    for i, target in enumerate(targets):
        average_instructions.append([target])

        total_instructions = 0
        total_latest_percentages: Optional[float] = 0.0
        total_reference_percentages = 0.0

        for instructions in results:
            total_instructions += int(instructions[i][1])
            if instructions[i][3] != "-----":
                total_reference_percentages += float(
                    str(instructions[i][3])[:-1])
            if total_latest_percentages is not None:
                if instructions[i][2] != "N/A":
                    if instructions[i][2] != "-----":
                        total_latest_percentages += float(
                            str(instructions[i][2])[:-1])
                else:
                    total_latest_percentages = None

        avg_instructions = total_instructions // num_benchmarks
        avg_reference_percentages = format_percentage(
            round(total_reference_percentages / num_benchmarks, 3))
        avg_latest_percentages = format_percentage(
            round(total_latest_percentages / num_benchmarks, 3)) \
            if total_latest_percentages is not None else "N/A"

        average_instructions[-1].extend([avg_instructions,
                                         avg_latest_percentages,
                                         avg_reference_percentages])

    return average_instructions


def write_to_csv(instructions: List[List[Union[str, int]]],
                 output_csv_path: str, percentages: bool = False,
                 reference_version: str = "") -> None:
    """
    Write the [Target, Instructions] for each target in a CSV file.
    comparison_instructions are ignored.

    Parameters:
    instructions (List[List[Union[str, int]]]):
            [[target_name, instructions, comparison_instructions],[...],...]
            comparsion_instructions: *[latest, reference]
            If latest is not available,
            then comparsion_instructions = reference
    output_csv_path (str): Absolute path to output CSV file
    percentages (bool): Add percentages to the output CSV file
    """
    with open(output_csv_path, "w") as file:
        writer = csv.writer(file)
        header = ["Target", "Instructions"]
        if percentages:
            header.extend(["Latest", reference_version])
        writer.writerow(header)
        for target_instructions in instructions:
            row = []
            row.extend([target_instructions[0], format(
                target_instructions[1], ",")])
            if percentages:
                row.extend(target_instructions[2:])
            writer.writerow(row)


def print_table(instructions: str, text: str, reference_version: str) -> None:
    """
    Print the results in a tabular form

    Parameters:
    instructions (List[List[Union[str, int]]]):
            [[target_name, instructions, comparison_instructions],[...],...]
            comparsion_instructions: *[latest, reference]
            If latest is not available,
            then comparsion_instructions = reference
    text (str): Text be added to the table header
    reference_version (str): Reference version used in these results
    """
    print("{}\n{}\n{}".
          format("-" * 56, text, "-" * 56))

    print('{:<10}  {:>20}  {:>10}  {:>10}\n{}  {}  {}  {}'.
          format('Target',
                 'Instructions',
                 'Latest',
                 reference_version,
                 '-' * 10,
                 '-' * 20,
                 '-' * 10,
                 '-' * 10))

    for target_change in instructions:
        # Replace commas with spaces in instruction count
        # for easier readability.
        formatted_instructions = format(
            target_change[1], ",").replace(",", " ")
        print('{:<10}  {:>20}  {:>10}  {:>10}'.format(
            target_change[0], formatted_instructions, *target_change[2:]))

    print("-" * 56)


def clean_exit(qemu_path: str, error_message: str) -> None:
    """
    Clean up intermediate files and exit.

    Parameters:
    qemu_path (str): Absolute path to QEMU
    error_message (str): Error message to display after exiting
    """
    # Clean the QEMU build path
    qemu_build_path = os.path.join(qemu_path, "build-gcc")
    if os.path.isdir(qemu_build_path):
        shutil.rmtree(qemu_build_path)
    sys.exit(error_message)


def verify_executables(benchmark_paths: List[str], targets: Dict[str, str],
                       benchmarks: List[Dict[str, str]],
                       benchmarks_executables_dir_path: str) -> None:
    """
    Verify that all executables exist for each benchmark.

    Parameters:
    benchmark_paths (List[str]): List of all paths to benchmarks
    targets (Dict[str, str]): Dictionary the contains for each target,
                              target_name: target_compiler
    benchmarks (List[Dict[str, str]]): Benchmarks data (name, parent_dir, path)
    benchmarks_executables_dir_path (str): Absolute path to the executables dir
    """
    print(datetime.datetime.utcnow().isoformat(),
          "- Verifying executables of {} benchmarks for {} targets".
          format(len(benchmark_paths), len(targets)),
          file=sys.stderr, flush=True)

    for benchmark in benchmarks:
        executable_parent_dir_path = get_executable_parent_dir_path(
            benchmark["path"], benchmarks_executables_dir_path)

        # Verify that the exists for this benchmark executables, if not,
        # create it
        if not os.path.isdir(executable_parent_dir_path):
            os.mkdir(executable_parent_dir_path)

        for target_name, target_compiler in targets.items():
            compiled_benchmark = "{}-{}".format(
                benchmark["name"], target_name)
            compiled_benchmark_path = os.path.join(
                executable_parent_dir_path, compiled_benchmark)
            # Verify that the the executable for this target is available,
            # if not, compile it
            if not os.path.isfile(compiled_benchmark_path):
                compile_target(benchmark["path"],
                               compiled_benchmark_path,
                               target_compiler)


def verify_reference_results(reference_version: str, qemu_path: str,
                             benchmarks: List[Dict[str, str]],
                             reference_version_results_dir_path: str,
                             targets: List[str],
                             benchmarks_executables_dir_path: str) -> None:
    """
    Verify that results are available for reference version.
    If results are missing, build QEMU for the reference version then perform
    the measurements.

    Paramters:
    reference_version (str): Reference QEMU version
    qemu_path (str): Absolute path to QEMU
    benchmark_paths (List[str]): List of all paths to benchmarks
    reference_version_results_dir_path (str): Absolute path to the reference
                                              version results dir
    targets (List[str]): Target names
    benchmarks (List[Dict[str, str]]): Benchmarks data (name, parent_dir, path)
    benchmarks_executables_dir_path (str): Path to the root executables dir
    """
    print(datetime.datetime.utcnow().isoformat(),
          "- Verifying results of reference version {}".
          format(reference_version), file=sys.stderr, flush=True)

    # Set flag to know if QEMU was built for reference version before
    did_build_reference = False

    latest_commit_hash = get_commit_hash(reference_version, qemu_path)

    for benchmark in benchmarks:
        benchmark_results_dir_path = os.path.join(
            reference_version_results_dir_path, benchmark["parent_dir"])

        # Verify that the results directory for the benchmark exists, if not,
        # create it
        if not os.path.isdir(benchmark_results_dir_path):
            os.mkdir(benchmark_results_dir_path)

        # Verify that the the results.csv file for the benchmark exits, if not,
        # create it
        results_path = os.path.join(benchmark_results_dir_path,
                                    "{}-{}-results.csv".
                                    format(latest_commit_hash,
                                           benchmark["name"]))
        if not os.path.isfile(results_path):
            # Only build qemu if reference version wasn't built before
            if not did_build_reference:
                build_qemu(qemu_path, reference_version, targets)
                did_build_reference = True
            print(datetime.datetime.utcnow().isoformat(),
                  "- Measuring instructions for reference version {} - {}".
                  format(reference_version, benchmark["name"]),
                  file=sys.stderr, flush=True)
            instructions = measure_instructions(
                benchmark["path"],
                benchmarks_executables_dir_path,
                qemu_path,
                targets)
            write_to_csv(instructions, results_path)


def verify_requirements() -> None:
    """
    Verify that all script requirements are installed (valgrind & git).
    """
    # Insure that valgrind is installed
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


def main():
    """
    Parse the command line arguments then start the execution.
    Output on STDOUT represents the nightly test results.
    Output on STDERR represents the execution log and errors if any.
    Output on STDERR must be redirected to either /dev/null or to a log file.

    Syntax:
        nightly_tests_core.py [-h] [-r REF]
    Optional arguments:
        -h, --help            Show this help message and exit
        -r REF, --reference REF
                            Reference QEMU version - Default is v5.1.0
    Example of usage:
        nightly_tests_core.py -r v5.1.0 2>log.txt
    """
    parser = argparse.ArgumentParser()
    parser.add_argument("-r", "--reference", dest="ref",
                        default="v5.1.0",
                        help="Reference QEMU version - Default is v5.1.0")
    reference_version = parser.parse_args().ref

    targets = {
        "aarch64":  "aarch64-linux-gnu-gcc",
        "alpha":    "alpha-linux-gnu-gcc",
        "arm":      "arm-linux-gnueabi-gcc",
        "hppa":     "hppa-linux-gnu-gcc",
        "m68k":     "m68k-linux-gnu-gcc",
        "mips":     "mips-linux-gnu-gcc",
        "mipsel":   "mipsel-linux-gnu-gcc",
        "mips64":   "mips64-linux-gnuabi64-gcc",
        "mips64el": "mips64el-linux-gnuabi64-gcc",
        "ppc":      "powerpc-linux-gnu-gcc",
        "ppc64":    "powerpc64-linux-gnu-gcc",
        "ppc64le":  "powerpc64le-linux-gnu-gcc",
        "riscv64":  "riscv64-linux-gnu-gcc",
        "s390x":    "s390x-linux-gnu-gcc",
        "sh4":      "sh4-linux-gnu-gcc",
        "sparc64":  "sparc64-linux-gnu-gcc",
        "x86_64":   "gcc"
    }

    # Verify that the script requirements are installed
    verify_requirements()

    # Get required paths
    nightly_tests_dir_path = pathlib.Path(__file__).parent.parent.absolute()
    benchmarks_dir_path = os.path.join(nightly_tests_dir_path, "benchmarks")
    benchmarks_source_dir_path = os.path.join(benchmarks_dir_path, "source")
    benchmarks_executables_dir_path = os.path.join(
        benchmarks_dir_path, "executables")

    # Verify that If the executables directory exists, if not, create it
    if not os.path.isdir(benchmarks_executables_dir_path):
        os.mkdir(benchmarks_executables_dir_path)

    # Get absolute path to all available benchmarks
    benchmark_paths = sorted([y for x in os.walk(benchmarks_source_dir_path)
                              for y in glob.glob(os.path.join(x[0], '*.c'))])

    benchmarks = [{
        "name": get_benchmark_name(benchmark_path),
        "parent_dir": get_benchmark_parent_dir(benchmark_path),
        "path": benchmark_path} for benchmark_path in benchmark_paths]

    # Verify that all executables exist for each benchmark
    verify_executables(benchmark_paths, targets, benchmarks,
                       benchmarks_executables_dir_path)

    # Set QEMU path and clone from Git if the path doesn't exist
    qemu_path = os.path.join(nightly_tests_dir_path, "qemu-nightly")
    if not os.path.isdir(qemu_path):
        # Clone QEMU into the temporary directory
        print(datetime.datetime.utcnow().isoformat(),
              "- Fetching QEMU: ", end="", flush=True, file=sys.stderr)
        git_clone(qemu_path)
        print("\n", file=sys.stderr, flush=True)

    # Verify that the results directory exists, if not, create it
    results_dir_path = os.path.join(nightly_tests_dir_path, "results")
    if not os.path.isdir(results_dir_path):
        os.mkdir(results_dir_path)

    # Verify that the reference version results directory exists, if not,
    # create it
    reference_version_results_dir_path = os.path.join(
        results_dir_path, reference_version)
    if not os.path.isdir(reference_version_results_dir_path):
        os.mkdir(reference_version_results_dir_path)

    # Verify that previous results are available for reference version
    verify_reference_results(reference_version, qemu_path, benchmarks,
                             reference_version_results_dir_path,
                             targets.keys(), benchmarks_executables_dir_path)

    # Compare results with the latest QEMU master
    # -------------------------------------------------------------------------
    # Verify that the "latest" results directory exists, if not,
    # create it
    latest_version_results_dir_path = os.path.join(results_dir_path, "latest")
    if not os.path.isdir(latest_version_results_dir_path):
        os.mkdir(latest_version_results_dir_path)

    # Verify that the "history" results directory exists, if not, create it
    history_results_dir_path = os.path.join(results_dir_path, "history")
    if not os.path.isdir(history_results_dir_path):
        os.mkdir(history_results_dir_path)

    # Build QEMU for master
    build_qemu(qemu_path, "master", targets.keys())

    # Get the commit hash for the top commit at master
    master_commit_hash = get_commit_hash("master", qemu_path)

    # Print report summary header
    print("{}\n{}".format("-" * 56, "{} SUMMARY REPORT - COMMIT {}".
                          format(" "*11, master_commit_hash)))

    # For each benchmark, compare the current master results with
    # latest and reference version
    results = []
    for benchmark in benchmarks:
        reference_version_benchmark_results_dir_path = os.path.join(
            reference_version_results_dir_path, benchmark["parent_dir"])
        latest_version_benchmark_results_dir_path = os.path.join(
            latest_version_results_dir_path, benchmark["parent_dir"])
        history_benchmark_results_dir_path = os.path.join(
            history_results_dir_path, benchmark["parent_dir"])

        # Verify that the the benchmark directory exists in the "latest"
        # directory, if not, create it
        if not os.path.isdir(latest_version_benchmark_results_dir_path):
            os.mkdir(latest_version_benchmark_results_dir_path)

        # Verify that the the benchmark directory exists in the "history"
        # directory, if not, create it
        if not os.path.isdir(history_benchmark_results_dir_path):
            os.mkdir(history_benchmark_results_dir_path)

        # Obtain the instructions array which will contain for each target,
        # the target name, the number of "master" instructions,
        # "latest" instructions if available,
        # and "reference version" instructions
        instructions = measure_master_instructions(
            reference_version_benchmark_results_dir_path,
            get_commit_hash(reference_version, qemu_path),
            latest_version_benchmark_results_dir_path,
            benchmark["path"],
            benchmarks_executables_dir_path,
            qemu_path,
            targets.keys())

        # Update the "latest" directory with the new results form master
        updated_latest_version_benchmark_results = os.path.join(
            latest_version_benchmark_results_dir_path,
            "{}-{}-results.csv".format(master_commit_hash, benchmark["name"]))
        write_to_csv(instructions, updated_latest_version_benchmark_results)

        calculate_change(instructions)

        # Insert "N/A" for targets that don't have "latest" instructions
        for target_instructions in instructions:
            if len(target_instructions) == 3:
                target_instructions.insert(2, "N/A")

        history_benchmark_results = os.path.join(
            history_benchmark_results_dir_path,
            "{}-{}-results.csv".format(master_commit_hash, benchmark["name"]))
        write_to_csv(instructions, history_benchmark_results,
                     True, reference_version)

        # Store the results
        results.append([benchmark["name"], instructions])

    # Calculate the average instructions for each target
    # Only send the instructions as results without the benchmark names
    average_instructions = calculate_average(
        [result[1] for result in results], targets.keys(), len(benchmarks))

    # Save average results to results/history directory
    average_results = os.path.join(
        history_results_dir_path,
        "{}-average-results.csv".format(master_commit_hash))
    write_to_csv(average_instructions, average_results,
                 True, reference_version)

    # Print results
    print_table(average_instructions, " "*20 +
                "AVERAGE RESULTS", reference_version)
    print("\n", " "*17, "DETAILED RESULTS")
    for [benchmark_name, instructions] in results:
        print_table(instructions,
                    "Test Program: " + benchmark_name,
                    reference_version)

    # Cleanup (delete the build directory)
    shutil.rmtree(os.path.join(qemu_path, "build-gcc"))
    # -------------------------------------------------------------------------


if __name__ == "__main__":
    main()
