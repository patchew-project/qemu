#!/bin/sh
# This work is licensed under the terms of the GNU LGPL, version 2 or later.
# See the COPYING.LIB file in the top-level directory.

PYTHON=$1
DECODETREE=$2
E_FILES=`echo err_*.decode`
S_FILES=`echo succ_*.decode`

j=0
for i in $E_FILES $S_FILES; do
    j=`expr $j + 1`
done

echo 1..$j

j=0
for i in $E_FILES; do
    j=`expr $j + 1`
    n=`basename $i .decode`
    if $PYTHON $DECODETREE $i > /dev/null 2> /dev/null; then
        # Failed to fail.
        echo not ok $j $n
    else
        echo ok $j $n
    fi
done

for i in $S_FILES; do
    j=`expr $j + 1`
    n=`basename $i .decode`
    if $PYTHON $DECODETREE $i > /dev/null 2> /dev/null; then
        # Succeeded.
        echo ok $j $n
    else
        echo not ok $j $n
    fi
done

exit 0
