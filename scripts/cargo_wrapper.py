#!/usr/bin/env python3
# Copyright (c) 2020 Red Hat, Inc.
#
# Author:
#  Marc-André Lureau <marcandre.lureau@gmail.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import argparse
import configparser
import distutils.file_util
import glob
import os
import os.path
import re
import subprocess
import sys
from typing import List


def get_cargo_target_dir(args: argparse.Namespace) -> str:
    # avoid conflict with qemu "target" directory
    return os.path.join(args.build_dir, "rs-target")


def get_manifest_path(args: argparse.Namespace) -> str:
    return os.path.join(args.src_dir, "Cargo.toml")


def build_lib(args: argparse.Namespace) -> None:
    target_dir = get_cargo_target_dir(args)
    manifest_path = get_manifest_path(args)
    # let's pretend it's an INI file to avoid extra dependency
    config = configparser.ConfigParser()
    config.read(manifest_path)
    package_name = config["package"]["name"].strip('"')
    liba = os.path.join(
        target_dir, args.target_triple, args.build_type, "lib" + package_name + ".a"
    )
    libargs = os.path.join(args.build_dir, "lib" + package_name + ".args")

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
    if args.target_triple:
        cargo_cmd += ["--target", args.target_triple]
    if args.build_type == "release":
        cargo_cmd += ["--release"]
    cargo_cmd += ["--", "--print", "native-static-libs"]
    cargo_cmd += args.EXTRA
    try:
        out = subprocess.check_output(
            cargo_cmd,
            env=dict(os.environ, **env),
            stderr=subprocess.STDOUT,
            universal_newlines=True,
        )
        native_static_libs = re.search(r"native-static-libs:(.*)", out)
        link_args = native_static_libs.group(1)
        with open(libargs, "w") as f:
            print(link_args, file=f)

        distutils.file_util.copy_file(liba, args.build_dir, update=True)
    except subprocess.CalledProcessError as e:
        print(
            "Environment: " + " ".join(["{}={}".format(k, v) for k, v in env.items()])
        )
        print("Command: " + " ".join(cargo_cmd))
        print(e.output)
        sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("command")
    parser.add_argument("build_dir")
    parser.add_argument("src_dir")
    parser.add_argument("build_root")
    parser.add_argument("build_type")
    parser.add_argument("target_triple")
    parser.add_argument("EXTRA", nargs="*")
    args = parser.parse_args()

    if args.command == "build-lib":
        build_lib(args)
    else:
        raise argparse.ArgumentTypeError("Unknown command: %s" % args.command)


if __name__ == "__main__":
    main()
