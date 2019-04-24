#!/bin/sh

# Honor the SPEED environment variable, just like we do it for the qtests.
# The default is to run all tests that still work fine in a CI environments,
# but if the user set SPEED=slow or SPEED=thorough, we also run all other
# tests that are still marked as "quick"
if [ "$SPEED" = "slow" -o "$SPEED" = "thorough" ]; then
    group=quick
else
    group=ci
fi

if [ -z "$(find . -name 'qemu-system-*' -print)" ]; then
    echo "No qemu-system binary available. Skipped qemu-iotests."
    exit 0
fi

cd tests/qemu-iotests

ret=0
TEST_DIR=${TEST_DIR:-/tmp/qemu-iotests-quick-$$} ./check -T -qcow2 -g "$group" || ret=1

exit $ret
