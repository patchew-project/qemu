#! /usr/bin/env sh
#
# Send an "Alive!" message regularly to stderr
#
# Copyright (C) 2017 Philippe Mathieu-Daudé
#
# Author: Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU LGPL, version 2+.
# See the COPYING file in the top-level directory.

TIMEOUT_S=$1
shift 1
{
  set -e
  while true
  do
    sleep ${TIMEOUT_S}
    echo "Alive!" >&2
  done
} &
WATCHDOG_PID=$!

cleanup() {
    echo "killing watchdog ${WATCHDOG_PID}" >&2
    kill -TERM ${WATCHDOG_PID}
    exit $((1 - $#))
}
trap cleanup INT

$*
cleanup 0
