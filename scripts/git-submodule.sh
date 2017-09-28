#!/bin/bash

set -e

command=$1
shift
modules="$@"

test -z "$modules" && exit 0

if ! test -d ".git"
then
    echo "$0: unexpectedly called with submodules but no git checkout exists"
    exit 1
fi

substat=".git-submodule-status"

case "$command" in
status)
    test -f "$substat" || exit 1
    git submodule status $modules > "${substat}.tmp"
    trap "rm -f ${substat}.tmp" EXIT
    diff "${substat}" "${substat}.tmp" >/dev/null
    exit $?
    ;;
update)
    git submodule update --init $modules 1>/dev/null 2>&1
    git submodule status $modules > "${substat}"
    ;;
esac
