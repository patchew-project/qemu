#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Run the bare-metal sPAPR reproducers and check the words each leaves
# behind.
#
# Each payload boots with -kernel on -M pseries, drives the hypervisor
# interface directly, and writes its results to physical 0x01000000.  This
# script reads that buffer back through the QEMU monitor and compares it
# against the expected words.
#
# Not wired into meson or CI, the same as tests/multiboot: it needs a
# machine, it takes a few seconds per payload, and its value is in being
# runnable by hand when someone doubts a claim.
#
# Usage:
#     ./run_test.sh [/path/to/qemu-system-ppc64]
#
# Exit status is 0 only if every assertion held.

set -u

QEMU=${1:-${QEMU:-../../build/qemu-system-ppc64}}
SETTLE=${SETTLE:-15}

if [ ! -x "$QEMU" ]; then
    echo "qemu-system-ppc64 not found at: $QEMU" >&2
    echo "pass the path as \$1 or set \$QEMU" >&2
    exit 2
fi

fail=0
pass=0

# Backing disk for vscsibig: sector s starts with the big-endian word
# 0xA5000000|s, so a chunk that lands at the wrong offset names the sector
# it really came from.  Generated rather than committed -- it is 2 MiB of
# almost nothing.
make_disk() {
    perl -e '
        open(my $f, ">", "vscsidisk.raw") or die;
        binmode $f;
        for my $s (0 .. 4095) {
            print $f pack("N", 0xA5000000 | $s), (chr(0) x 508);
        }
        close $f;
    '
}

# $1 payload  $2 words to dump  $3.. "index:expected:description"
run_payload() {
    local name=$1 words=$2; shift 2
    local out="$name.monitor.txt"

    ( sleep "$SETTLE"; printf 'x /%dxw 0x1000000\nquit\n' "$words" ) | \
        "$QEMU" -M pseries -cpu POWER7 -m 512 \
            -display none -vga none -nodefaults -serial none \
            -kernel "$name.elf" \
            -device spapr-vscsi,id=scsi0 \
            -drive file=vscsidisk.raw,if=none,id=d0,format=raw \
            -device scsi-hd,bus=scsi0.0,drive=d0 \
            -monitor stdio >"$out" 2>/dev/null

    # The monitor prints "0000000001000000: 0x00000000 0x00000000 ..." and,
    # being a readline monitor on a pipe, echoes every typed character
    # wrapped in cursor-control escapes.  Pulling out each 0x-prefixed
    # 8-digit token sidesteps both: the echoed command contains 0x1000000,
    # which is seven digits and does not match.
    local got n
    got=$(grep -oE '0x[0-9a-f]{8}' "$out" | sed 's/^0x//')
    n=$(printf '%s\n' "$got" | grep -c .)

    if [ "$n" -lt "$words" ]; then
        echo "  $name: NOTHING MEASURED -- expected $words words, got $n"
        echo "        A short read means the payload did not finish or the"
        echo "        dump failed.  Raise \$SETTLE and look at $out."
        fail=$((fail + 1))
        return
    fi

    local spec idx expect desc actual
    for spec in "$@"; do
        idx=${spec%%:*}; spec=${spec#*:}
        expect=${spec%%:*}; desc=${spec#*:}
        actual=$(echo "$got" | sed -n "$((idx + 1))p")
        if [ "$actual" = "$expect" ]; then
            echo "  PASS  $desc"
            pass=$((pass + 1))
        else
            echo "  FAIL  $desc"
            echo "        expected $expect, got ${actual:-<nothing>}"
            fail=$((fail + 1))
        fi
    done
}

echo "=== vscsibig: a >128 KiB transfer must not be folded onto the"
echo "    start of the guest buffer ==="
make_disk
run_payload vscsibig 20 \
    "1:00000000:H_REG_CRQ registered the queue on the vSCSI adapter" \
    "11:00000000:the 256 KiB READ_10 reports SRP status 0 either way" \
    "13:a5000000:sector 0 landed at buffer offset 0" \
    "15:a5000100:sector 256 landed at buffer offset 0x20000"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
