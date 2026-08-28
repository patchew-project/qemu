#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

targets()
{
    ./build/qemu-system -target help |& grep '^-' | sed -e 's/-//g'
}

filter_version()
{
    grep -v 'QEMU emulator version'
}

filter_usage()
{
    grep -v '^usage:' | grep -v '^-target' || true
}

filter_trailing_whitespace()
{
    sed -e 's/\s+$//'
}

f()
{
    filter_version | filter_usage | filter_trailing_whitespace
}

ninja -C build all qemu-system

ref=cmdline_ref
err=0
for t in $(targets); do
    echo ------------------------------------------------------------------
    git diff --color-words --no-index \
        <(./build/qemu-system-$t -help |& f) \
        <(./build/qemu-system -target $t -help |& f) || err=1
    git diff --color-words --no-index \
        <(./build/qemu-system-$t -cpu help |& f) \
        <(./build/qemu-system -target $t -cpu help |& f) || err=1
    git diff --color-words --no-index \
        <(./build/qemu-system-$t -machine help |& f) \
        <(./build/qemu-system -target $t -machine help |& f) || err=1
    git diff --color-words --no-index \
        <(./build/qemu-system-$t -device help |& f) \
        <(./build/qemu-system -target $t -device help |& f) || err=1
done

exit $err
