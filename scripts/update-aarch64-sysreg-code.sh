#!/bin/sh -e
#
# SPDX-License-Identifier: GPL-2.0-or-later
# Update target/arm/cpu-sysregs.h
# from a linux source tree (arch/arm64/tools/sysreg)
#
# Copyright Red Hat, Inc. 2024
#
# Authors:
#          Eric Auger <eric.auger@redhat.com>
#

scripts="$(dirname "$0")"
linux="$1"
output="$2"

if [ -z "$linux" ] || ! [ -d "$linux" ]; then
    cat << EOF
usage: update-aarch64-sysreg-code.sh LINUX_PATH [OUTPUT_PATH]

LINUX_PATH      Linux kernel directory to obtain the register definitions from
OUTPUT_PATH     output directory, usually the qemu source tree (default: $PWD)
EOF
    exit 1
fi

if [ -z "$output" ]; then
    output="$PWD"
fi

awk -f $scripts/arm-gen-cpu-sysregs-header.awk \
    $linux/arch/arm64/tools/sysreg > $output/target/arm/cpu-sysregs.h.inc
