#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

binaries()
{
    ls configs/targets/ |
    grep -v meson.build |
    grep -v bsd-user |
    sed -e 's/\..*//' \
        -e 's/\(.*\)-linux-user/qemu-\1/' \
        -e 's/\(.*\)-softmmu/qemu-system-\1/'
}

filter_version()
{
    grep -v 'QEMU emulator version' || true
}

filter_usage()
{
    grep -v '^usage:' | grep -v '^-target' || true
}

filter_trailing_whitespace()
{
    sed -e 's/\s*$//'
}

f()
{
    filter_version | filter_usage | filter_trailing_whitespace
}

ninja -C build all

ref=cmdline_ref
rm -rf $ref
mkdir -p $ref
for bin in $(binaries); do
    echo "$bin"
    ./build/$bin -help |& f         > $ref/$bin.help
    (./build/$bin -cpu help |& f || true) > $ref/$bin.cpu_help
    if echo $bin | grep -q qemu-system; then
        ./build/$bin -machine help |& f > $ref/$bin.machine_help
        ./build/$bin -device help |& f  > $ref/$bin.device_help
    fi
done
