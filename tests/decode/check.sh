#!/bin/sh

PYTHON=$1
DECODETREE=$2
E=0

# All of these tests should produce errors
for i in err_*.def; do
    if $PYTHON $DECODETREE $i > /dev/null 2> /dev/null; then
        # Pass, aka failed to fail.
        echo FAIL: $i 1>&2
        E=1
    fi
done

exit $E
