#!/usr/bin/env python3

"""Wrap cargo builds for meson integration

This program builds Rust library crates and makes sure:
 - They receive the correct --cfg compile flags from the QEMU build that calls
   it.
 - They receive the generated Rust bindings path so that they can copy it
   inside their output subdirectories.
 - Cargo puts all its build artifacts in the appropriate meson build directory.
 - The produced static libraries are copied to the path the caller (meson)
   defines.

Copyright (c) 2020 Red Hat, Inc.
Copyright (c) 2024 Linaro Ltd.

Authors:
 Marc-André Lureau <marcandre.lureau@redhat.com>
 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""


import argparse
import json
import logging
import os
import subprocess
import sys
import shutil

from pathlib import Path
from typing import Any, Dict, List


def generate_cfg_flags(header: str) -> List[str]:
    """Converts defines from config[..].h headers to rustc --cfg flags."""

    def cfg_name(name: str) -> str:
        """Filter function for C #defines"""
        if (
            name.startswith("CONFIG_")
            or name.startswith("TARGET_")
            or name.startswith("HAVE_")
        ):
            return name
        return ""

    with open(header, encoding="utf-8") as cfg:
        config = [l.split()[1:] for l in cfg if l.startswith("#define")]

    cfg_list = []
    for cfg in config:
        name = cfg_name(cfg[0])
        if not name:
            continue
        if len(cfg) >= 2 and cfg[1] != "1":
            continue
        cfg_list.append("--cfg")
        cfg_list.append(name)
    return cfg_list


def cargo_target_dir(args: argparse.Namespace) -> Path:
    """Place cargo's build artifacts into meson's build directory"""
    return args.private_dir


def manifest_path(args: argparse.Namespace) -> Path:
    """Returns the Cargo.toml manifest path"""
    return args.crate_dir / "Cargo.toml"


def get_cargo_rustc(args: argparse.Namespace) -> tuple[Dict[str, Any], List[str]]:
    """Returns the appropriate cargo invocation and environment"""

    # See https://doc.rust-lang.org/cargo/reference/environment-variables.html
    # Item `CARGO_ENCODED_RUSTFLAGS — A list of custom flags separated by
    # 0x1f (ASCII Unit Separator) to pass to all compiler invocations that Cargo
    # performs`
    cfg = chr(0x1F).join(
        [c for h in args.config_headers for c in generate_cfg_flags(h)]
    )
    target_dir = cargo_target_dir(args)
    cargo_path = manifest_path(args)

    cargo_cmd = [
        "cargo",
        "build",
        "--target-dir",
        str(target_dir),
        "--manifest-path",
        str(cargo_path),
    ]
    if args.target_triple:
        cargo_cmd += ["--target", args.target_triple]
    if args.profile == "release":
        cargo_cmd += ["--release"]

    env = os.environ
    env["CARGO_ENCODED_RUSTFLAGS"] = cfg
    env["MESON_BUILD_DIR"] = str(target_dir)
    env["MESON_BUILD_ROOT"] = str(args.meson_build_root)

    return (env, cargo_cmd)


def run_cargo(env: Dict[str, Any], cargo_cmd: List[str]) -> str:
    """Calls cargo build invocation."""
    envlog = " ".join([f"{k}={v}" for k, v in env.items()])
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


def get_package_name(cargo_toml_path: Path) -> str:
    """Attempts to get package name from cargo manifest file with toml parsing libraries."""
    # pylint: disable=import-outside-toplevel

    try:
        import tomllib
    except ImportError:
        import tomli as tomllib
    with open(cargo_toml_path, "rb") as toml_file:
        config = tomllib.load(toml_file)

    package_name = config["package"]["name"].strip('"').replace("-", "_")
    return package_name


def get_package_name_json(cargo_toml_path: Path) -> str:
    """Attempts to get package name from cargo-metadata output which has a standard JSON format."""

    cmd = [
        "cargo",
        "metadata",
        "--format-version",
        "1",
        "--no-deps",
        "--manifest-path",
        str(cargo_toml_path),
        "--offline",
    ]
    try:
        out = subprocess.check_output(
            cmd,
            env=os.environ,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
        )
    except subprocess.CalledProcessError as err:
        print("Command: ", " ".join(cmd))
        print(err.output)
        raise err
    package_name = json.loads(out)["packages"][0]["name"].strip('"').replace("-", "_")
    return package_name


def build_lib(args: argparse.Namespace) -> None:
    """Builds Rust lib given by command line arguments."""

    logging.debug("build-lib")
    target_dir = cargo_target_dir(args)
    cargo_toml_path = manifest_path(args)

    try:
        # If we have tomllib or tomli, parse the .toml file
        package_name = get_package_name(cargo_toml_path)
    except ImportError as import_exc:
        try:
            # Parse the json output of cargo-metadata as a fallback
            package_name = get_package_name_json(cargo_toml_path)
        except Exception as exc:
            raise exc from import_exc

    liba_filename = "lib" + package_name + ".a"
    profile_dir = args.profile
    if args.profile == "dev":
        profile_dir = "debug"

    liba = target_dir / args.target_triple / profile_dir / liba_filename

    env, cargo_cmd = get_cargo_rustc(args)
    out = run_cargo(env, cargo_cmd)
    logging.debug("cargo output: %s", out)
    logging.debug("cp %s %s", liba, args.outdir)
    shutil.copy2(liba, args.outdir)


def main() -> None:
    # pylint: disable=missing-function-docstring
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument(
        "--color",
        metavar="WHEN",
        choices=["auto", "always", "never"],
        default="auto",
        help="Coloring: auto, always, never",
    )
    parser.add_argument(
        "--config-headers",
        metavar="CONFIG_HEADER",
        action="append",
        dest="config_headers",
        help="paths to any configuration C headers (*.h files), if any",
        required=False,
        default=[],
    )
    parser.add_argument(
        "--meson-build-root",
        metavar="BUILD_ROOT",
        help="meson.project_build_root(): the root build directory. Example: '/path/to/qemu/build'",
        type=Path,
        dest="meson_build_root",
        required=True,
    )
    parser.add_argument(
        "--crate-dir",
        metavar="CRATE_DIR",
        type=Path,
        dest="crate_dir",
        help="Absolute path that contains the manifest file of the crate to compile. Example: '/path/to/qemu/rust/pl011'",
        required=True,
    )
    parser.add_argument(
        "--outdir",
        metavar="OUTDIR",
        type=Path,
        dest="outdir",
        help="Destination path to copy compiled artifacts to for Meson to use. Example values: '/path/to/qemu/build', '.'",
        required=True,
    )
    # using @PRIVATE_DIR@ is necessary for `ninja clean` to clean up rust's intermediate build artifacts.
    # NOTE: at the moment cleanup doesn't work due to a bug: https://github.com/mesonbuild/meson/issues/7584
    parser.add_argument(
        "--private-dir",
        metavar="PRIVATE_DIR",
        type=Path,
        dest="private_dir",
        help="Override cargo's target directory with a meson provided private directory.",
        required=True,
    )
    parser.add_argument(
        "--profile", type=str, choices=["release", "dev"], required=True
    )
    parser.add_argument("--target-triple", type=str, required=True)

    subparsers = parser.add_subparsers()

    buildlib = subparsers.add_parser("build-lib")
    buildlib.set_defaults(func=build_lib)

    args = parser.parse_args()
    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)
    logging.debug("args: %s", args)

    args.func(args)


if __name__ == "__main__":
    main()
