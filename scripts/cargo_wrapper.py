#!/usr/bin/env python3
# Copyright (c) 2020 Red Hat, Inc.
# Copyright (c) 2023 Linaro Ltd.
#
# Authors:
#  Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
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
import pathlib
import shutil
import tomllib

from pathlib import Path
from typing import Any, Dict, List, Tuple

RUST_TARGET_TRIPLES = (
    "aarch64-unknown-linux-gnu",
    "x86_64-unknown-linux-gnu",
    "x86_64-apple-darwin",
    "aarch64-apple-darwin",
)


def cfg_name(name: str) -> str:
    if (
        name.startswith("CONFIG_")
        or name.startswith("TARGET_")
        or name.startswith("HAVE_")
    ):
        return name
    return ""


def generate_cfg_flags(header: str) -> List[str]:
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


def cargo_target_dir(args: argparse.Namespace) -> pathlib.Path:
    return args.meson_build_dir


def manifest_path(args: argparse.Namespace) -> pathlib.Path:
    return args.crate_dir / "Cargo.toml"


def get_cargo_rustc(args: argparse.Namespace) -> tuple[Dict[str, Any], List[str]]:
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
    logging.debug("build-lib")
    target_dir = cargo_target_dir(args)
    cargo_toml_path = manifest_path(args)

    with open(cargo_toml_path, "rb") as f:
        config = tomllib.load(f)

    package_name = config["package"]["name"].strip('"').replace("-", "_")

    liba_filename = "lib" + package_name + ".a"
    liba = target_dir / args.target_triple / args.profile / liba_filename

    env, cargo_cmd = get_cargo_rustc(args)
    out = run_cargo(env, cargo_cmd)
    logging.debug("cp %s %s", liba, args.outdir)
    shutil.copy2(liba, args.outdir)


def main() -> None:
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
        required=False,
        default=[],
    )
    parser.add_argument(
        "--meson-build-dir",
        metavar="BUILD_DIR",
        help="meson.current_build_dir()",
        type=pathlib.Path,
        dest="meson_build_dir",
        required=True,
    )
    parser.add_argument(
        "--meson-source-dir",
        metavar="SOURCE_DIR",
        help="meson.current_source_dir()",
        type=pathlib.Path,
        dest="meson_source_dir",
        required=True,
    )
    parser.add_argument(
        "--crate-dir",
        metavar="CRATE_DIR",
        type=pathlib.Path,
        dest="crate_dir",
        help="Absolute path that contains the manifest file of the crate to compile",
        required=True,
    )
    parser.add_argument(
        "--outdir",
        metavar="OUTDIR",
        type=pathlib.Path,
        dest="outdir",
        help="Path to copy compiled artifacts to for Meson to use.",
        required=True,
    )
    parser.add_argument(
        "--profile", type=str, choices=["release", "debug"], required=True
    )
    parser.add_argument(
        "--target-triple", type=str, choices=RUST_TARGET_TRIPLES, required=True
    )

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
