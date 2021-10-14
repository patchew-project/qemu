#! /usr/bin/env python3

# Generate configure command line options handling code, based on Meson's
# user build options introspection data
#
# Copyright (C) 2021 Red Hat, Inc.
#
# Author: Paolo Bonzini <pbonzini@redhat.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

import json
import textwrap
import shlex
import sys

def sh_print(line=""):
    print('  printf "%s\\n"', shlex.quote(line))


def load_options(json):
    json = [
        x
        for x in json
        if x["section"] == "user"
        and ":" not in x["name"]
        and x["name"] not in SKIP_OPTIONS
    ]
    return sorted(json, key=lambda x: x["name"])


def print_help(options):
    print("meson_options_help() {")
    sh_print()
    sh_print("Optional features, enabled with --enable-FEATURE and")
    sh_print("disabled with --disable-FEATURE, default is enabled if available")
    sh_print("(unless built with --without-default-features):")
    sh_print()
    print("}")


def print_parse(options):
    print("_meson_option_parse() {")
    print("  case $1 in")
    print("    *) return 1 ;;")
    print("  esac")
    print("}")


def fixup_options(options):
    # Meson <= 0.60 does not include the choices in array options, fix that up
    for opt in options:
        if opt["name"] == "trace_backends":
            opt["choices"] = [
                "dtrace",
                "ftrace",
                "log",
                "nop",
                "simple",
                "syslog",
                "ust",
            ]


options = load_options(json.load(sys.stdin))
fixup_options(options)
print("# This file is generated by meson-buildoptions.py, do not edit!")
print_help(options)
print_parse(options)
