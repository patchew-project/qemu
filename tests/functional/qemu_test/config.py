# Test class and utilities for functional tests
#
# Copyright 2018, 2024 Red Hat, Inc.
#
# Original Author (Avocado-based tests):
#  Cleber Rosa <crosa@redhat.com>
#
# Adaption for standalone version:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
from pathlib import Path
import platform


def _source_dir():
    # Determine top-level directory of the QEMU sources
    return Path(__file__).parent.parent.parent.parent

def _build_dir():
    root = os.getenv('MESON_BUILD_ROOT')
    if root is not None:
        return Path(root)

    raise Exception("Missing MESON_BUILD_ROOT environment variable. " +
                    "Please use the '<BUILD-DIR>/run' script if invoking " +
                    "directly instead of via make/meson")

BUILD_DIR = _build_dir()

def dso_suffix():
    '''Return the dynamic libraries suffix for the current platform'''

    if platform.system() == "Darwin":
        return "dylib"

    if platform.system() == "Windows":
        return "dll"

    return "so"
