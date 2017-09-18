#!/bin/sh
#
# Author: Fam Zheng <famz@redhat.com>
#
# Archive source tree, including submodules. This is created for test code to
# export the source files, in order to be built in a different enviornment,
# such as in a docker instance or VM.
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.

set -e

if test $# -lt 1; then
    echo "Usage: $0 <output tarball>"
    exit 1
fi

submodules=$(git submodule foreach --recursive --quiet 'echo $name')

if test -n "$submodules"; then
    {
        git ls-files
        for sm in $submodules; do
            (cd $sm; git ls-files) | sed "s:^:$sm/:"
        done
    } | grep -x -v $(for sm in $submodules; do echo "-e $sm"; done) > "$1".list
else
    git ls-files > "$1".list
fi

tar -cf "$1" -T "$1".list
rm "$1".list
