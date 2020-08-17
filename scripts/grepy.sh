#!/bin/sh

grep --no-filename '=y$' "$@" | sort -u
