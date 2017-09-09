#!/bin/sh
#
# Author: Fam Zheng <famz@redhat.com>
#
# Create archive of source tree, including submodules
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.

set -e

if test $# -lt 1; then
    echo "Usage: $0 <output>"
    exit 1
fi

submodules=$(git submodule foreach --recursive --quiet 'echo $name')

if test -n "$submodules"; then
    {
        git ls-files
        for sm in $submodules; do
            (cd $sm; git ls-files) | sed "s:^:$sm/:"
        done
    } | grep -x -v $(for sm in $submodules; do echo "-e $sm"; done) > $1.list
else
    git ls-files > $1.list
fi

tar -cf $1 -T $1.list
rm $1.list
