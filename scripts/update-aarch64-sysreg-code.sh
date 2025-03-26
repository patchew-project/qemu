#!/bin/sh -e
#
# Update target/arm/cpu-sysregs.h
# from a linux source tree (arch/arm64/tools/sysreg)
#
# Copyright Red Hat, Inc. 2024
#
# Authors:
#          Eric Auger <eric.auger@redhat.com>
#

linux="$1"
output="$PWD"

if [ -z "$linux" ] || ! [ -d "$linux" ]; then
    cat << EOF
usage: update-aarch64-sysreg-code.sh LINUX_PATH

LINUX_PATH      Linux kernel directory to obtain the headers from
EOF
    exit 1
fi

awk -f gen-cpu-sysregs-header.awk \
    $linux/arch/arm64/tools/sysreg > ../target/arm/cpu-sysregs.h.inc
