#!/bin/bash
#
# Author: Fam Zheng <famz@redhat.com>
#
# Archive source tree, including submodules. This is created for test code to
# export the source files, in order to be built in a different environment,
# such as in a docker instance or VM.
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.

error() {
    printf %s\\n "$*" >&2
    exit 1
}

if test $# -lt 1; then
    error "Usage: $0 <output tarball>"
fi

tar_file="$1"
list_file="$1.list"

trap "status=$?; rm -f \"$list_file\"; exit \$status" 0 1 2 3 15

( git ls-files && echo '.git' ) > "$list_file"

if test $? -ne 0; then
    error "failed to generate list file"
fi

tar -cf "$tar_file" -T "$list_file" || error "failed to create tar file"

exit 0
