#!/bin/bash
#
# Author: Fam Zheng <famz@redhat.com>
#
# Archive source tree, including submodules. This is created for test code to
# export the source files, in order to be built in a different enviornment,
# such as in a docker instance or VM.
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.

error() {
    echo "$@" >&2
    exit 1
}

if test $# -lt 1; then
    error "Usage: $0 <output tarball>"
fi

submodules=$(git submodule foreach --recursive --quiet 'echo $name')

if test $? -ne 0; then
    error "git submodule command failed"
fi

if test -n "$submodules"; then
    {
        git ls-files || error "git ls-files failed"
        for sm in $submodules; do
            (cd $sm; git ls-files) | sed "s:^:$sm/:"
            if test ${PIPESTATUS[0]} -ne 0 -o $? -ne 0; then
                error "git ls-files in submodule $sm failed"
            fi
        done
    } | grep -x -v $(for sm in $submodules; do echo "-e $sm"; done) > "$1".list
else
    git ls-files > "$1".list
fi

if test $? -ne 0; then
    error "failed to generate list file"
fi

tar -cf "$1" -T "$1".list
status=$?
rm "$1".list
if test $statue -ne 0; then
    error "failed to create tar file"
fi
exit 0
