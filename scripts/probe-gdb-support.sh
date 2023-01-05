#!/bin/sh

# Probe gdb for supported architectures.
#
# This is required to support testing of the gdbstub as its hard to
# handle errors gracefully during the test. Instead this script when
# passed a GDB binary will probe its architecture support and return a
# string of supported arches, stripped of guff.
#
# Copyright 2023 Linaro Ltd
#
# Author: Alex Bennée <alex.bennee@linaro.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

if test -z "$1"; then
  echo "Usage: $0 /path/to/gdb"
  exit 1
fi

# Start gdb with a set-architecture and capture the set of valid
# options.

valid_args=$($1 -ex "set architecture" -ex "quit" 2>&1 >/dev/null)

# Strip off the preamble
raw_arches=$(echo "${valid_args}" | sed "s/.*Valid arguments are \(.*\)/\1/")

# Split into lines, strip everything after :foo and return final
# "clean" list of supported arches.
final_arches=$(echo "${raw_arches}" | tr ', ' '\n' | sed "s/:.*//" | sort | uniq)

echo "$final_arches"
