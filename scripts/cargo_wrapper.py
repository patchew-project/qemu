#!/usr/bin/env python3
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Marc-André Lureau <marcandre.lureau@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import argparse
import configparser
import distutils.file_util
import json
import logging
import os
import os.path
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Tuple

from configh_to_cfg import generate_cfg


def get_rustc_target_spec(target_triple: str) -> Any:
    cmd = [
        "rustc",
        "+nightly",
        "-Z",
        "unstable-options",
        "--print",
        "target-spec-json",
    ]
    if target_triple:
        cmd += ["--target", target_triple]

    out = subprocess.check_output(cmd)
    return json.loads(out)


def get_exe_suffix(target_triple: str) -> str:
    try:
        # this will fail if nightly is not installed
        spec = get_rustc_target_spec(target_triple)
        return spec.get("exe-suffix", "")
    except: # pylint: disable=W0702
        # let's implement a simple fallback
        if 'windows' in target_triple:
            return '.exe'
        return ''


def get_cargo_target_dir(args: argparse.Namespace) -> str:
    # avoid conflict with qemu "target" directory
    return os.path.join(args.build_dir, "rs-target")


def get_manifest_path(args: argparse.Namespace) -> str:
    return os.path.join(args.src_dir, "Cargo.toml")


def get_cargo_rustc(
    cargo_rustc_args: List[str], args: argparse.Namespace
) -> Tuple[Dict[str, Any], List[str]]:
    cfg = [c for h in args.configh for c in generate_cfg(h)]
    target_dir = get_cargo_target_dir(args)
    manifest_path = get_manifest_path(args)

    env = {}
    env["MESON_CURRENT_BUILD_DIR"] = args.build_dir
    env["MESON_BUILD_ROOT"] = args.build_root
    env["WINAPI_NO_BUNDLED_LIBRARIES"] = "1"
    cargo_cmd = [
        "cargo",
        "rustc",
        "--target-dir",
        target_dir,
        "--manifest-path",
        manifest_path,
        "--offline",
    ]
    cargo_cmd += cargo_rustc_args
    if args.target_triple:
        cargo_cmd += ["--target", args.target_triple]
    if args.build_type == "release":
        cargo_cmd += ["--release"]
    cargo_cmd += ["--"] + cfg + args.EXTRA

    return (env, cargo_cmd)


def run_cargo(env: Dict[str, Any], cargo_cmd: List[str]) -> str:
    envlog = " ".join(["{}={}".format(k, v) for k, v in env.items()])
    cmdlog = " ".join(cargo_cmd)
    logging.debug("Running %s %s", envlog, cmdlog)
    try:
        out = subprocess.check_output(
            cargo_cmd,
            env=dict(os.environ, **env),
            stderr=subprocess.STDOUT,
            universal_newlines=True,
        )
    except subprocess.CalledProcessError as err:
        print("Environment: " + envlog)
        print("Command: " + cmdlog)
        print(err.output)
        sys.exit(1)

    return out


def build_lib(args: argparse.Namespace) -> None:
    logging.debug('build-lib')
    target_dir = get_cargo_target_dir(args)
    manifest_path = get_manifest_path(args)
    # let's pretend it's an INI file to avoid extra toml dependency
    config = configparser.ConfigParser()
    config.read(manifest_path)
    package_name = config["package"]["name"].strip('"').replace('-', '_')
    liba = os.path.join(
        target_dir, args.target_triple, args.build_type, "lib" + package_name + ".a"
    )
    libargs = os.path.join(args.build_dir, "lib" + package_name + ".args")

    env, cargo_cmd = get_cargo_rustc(["--lib"], args)
    cargo_cmd += ["--print", "native-static-libs"]
    out = run_cargo(env, cargo_cmd)
    native_static_libs = re.search(r"native-static-libs:(.*)", out)
    link_args = native_static_libs.group(1)
    with open(libargs, "w") as file:
        print(link_args, file=file)
    logging.debug("cp %s %s", liba, args.build_dir)
    distutils.file_util.copy_file(liba, args.build_dir, update=True)


def build_bin(args: argparse.Namespace) -> None:
    logging.debug('build-bin')
    env, cargo_cmd = get_cargo_rustc(["--bin", args.bin], args)
    exe_suffix = get_exe_suffix(args.target_triple)
    run_cargo(env, cargo_cmd)
    target_dir = get_cargo_target_dir(args)
    path = os.path.join(
        target_dir, args.target_triple, args.build_type, args.bin + exe_suffix
    )
    dest = args.build_dir
    if args.rename:
        dest = Path(dest) / args.rename
    logging.debug("cp %s %s", path, dest)
    distutils.file_util.copy_file(path, dest, update=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action='store_true')
    parser.add_argument("--configh", action='append', default=[])
    parser.add_argument("build_dir")
    parser.add_argument("src_dir")
    parser.add_argument("build_root")
    parser.add_argument("build_type")
    parser.add_argument("target_triple")
    subparsers = parser.add_subparsers()

    buildlib = subparsers.add_parser("build-lib")
    buildlib.add_argument("EXTRA", nargs="*")
    buildlib.set_defaults(func=build_lib)

    buildbin = subparsers.add_parser("build-bin")
    buildbin.add_argument("--rename")
    buildbin.add_argument("bin")
    buildbin.add_argument("EXTRA", nargs="*")
    buildbin.set_defaults(func=build_bin)

    args = parser.parse_args()
    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)
    logging.debug('args: %s', args)

    args.func(args)


if __name__ == "__main__":
    main()
