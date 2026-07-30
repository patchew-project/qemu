#!/usr/bin/env python3

##
##  Copyright(c) 2026 rev.ng Labs Srl. All Rights Reserved.
##
##  This program is free software; you can redistribute it and/or modify
##  it under the terms of the GNU General Public License as published by
##  the Free Software Foundation; either version 2 of the License, or
##  (at your option) any later version.
##
##  This program is distributed in the hope that it will be useful,
##  but WITHOUT ANY WARRANTY; without even the implied warranty of
##  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
##  GNU General Public License for more details.
##
##  You should have received a copy of the GNU General Public License
##  along with this program; if not, see <http://www.gnu.org/licenses/>.
##

import argparse
import json
import os
import shlex
import sys
import subprocess


def log(msg):
    print(msg, file=sys.stderr)


def run_command(command):
    proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = proc.communicate()
    if proc.wait() != 0:
        log(f"Command: {' '.join(command)} exited with {proc.returncode}\n")
        log(f"output:\n{out}\n")


def find_compile_commands(compile_commands_path, clang_path, input_path, target):
    with open(compile_commands_path, "r") as f:
        compile_commands = json.load(f)
        for compile_command in compile_commands:
            path = compile_command["file"]
            if os.path.basename(path) != os.path.basename(input_path):
                continue

            os.chdir(compile_command["directory"])
            command = compile_command["command"]

            # If building multiple targets there's a chance
            # input files share the same path and name.
            # This could cause us to find the wrong compile
            # command, we use the target path to distinguish
            # between these.
            if not target in command:
                continue

            argv = shlex.split(command)
            argv[0] = clang_path

            return argv

    raise ValueError(f"Unable to find compile command for {input_path}")


def generate_llvm_ir(
    compile_commands_path, clang_path, output_path, input_path, target
):
    command = find_compile_commands(
        compile_commands_path, clang_path, input_path, target
    )

    flags_to_remove = {
        "-ftrivial-auto-var-init=zero",
        "-fzero-call-used-regs=used-gpr",
        "-Wimplicit-fallthrough=2",
        "-Wold-style-declaration",
        "-Wno-psabi",
        "-Wshadow=local",
        "-c",
    }

    # Remove
    #   - output of makefile rules (-MQ,-MF target);
    #   - output of object files (-o target);
    #   - excessive zero-initialization of block-scope variables
    #     (-ftrivial-auto-var-init=zero);
    #   - and any optimization flags (-O).
    for i, arg in reversed(list(enumerate(command))):
        if arg in {"-MQ", "-o", "-MF"}:
            del command[i : i + 2]
        elif arg.startswith("-O") or arg in flags_to_remove:
            del command[i]

    # Define a HELPER_TO_TCG macro for translation units wanting to
    # conditionally include or exclude code during translation to TCG.
    # Disable optimization (-O0) and make sure clang doesn't emit optnone
    # attributes (-disable-O0-optnone) which inhibit further optimization.
    # Optimization will be performed at a later stage in the helper-to-tcg
    # pipeline.
    command += [
        "-S",
        "-emit-llvm",
        "-DHELPER_TO_TCG_IR_GEN",
        "-O0",
        "-g",
        "-Xclang",
        "-disable-O0-optnone",
    ]
    if output_path:
        command += ["-o", output_path]

    run_command(command)


def main():
    parser = argparse.ArgumentParser(
        description="Produce the LLVM IR of a given .c file."
    )
    parser.add_argument(
        "--compile-commands", required=True, help="Path to compile_commands.json"
    )
    parser.add_argument("--clang", default="clang", help="Path to clang.")
    parser.add_argument("--llvm-link", default="llvm-link", help="Path to llvm-link.")
    parser.add_argument("-o", "--output", required=True, help="Output .ll file path")
    parser.add_argument(
        "--target-path", help="Path to QEMU target dir. (e.q. target/i386)"
    )
    parser.add_argument("inputs", nargs="+", help=".c file inputs")
    args = parser.parse_args()

    outputs = []
    for input in args.inputs:
        output = os.path.basename(input) + ".ll"
        generate_llvm_ir(
            args.compile_commands, args.clang, output, input, args.target_path
        )
        outputs.append(output)

    run_command([args.llvm_link] + outputs + ["-S", "-o", args.output])


if __name__ == "__main__":
    sys.exit(main())
