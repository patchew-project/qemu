#!/bin/sh
# Build vdso using cross tools

build_dir=error
source_dir=error
target_dir=error
output=error

while test $# -gt 0; do
  opt="$1"
  shift
  case "$opt" in
    -B) build_dir=$1; shift;;
    -C) source_dir=$1; shift;;
    -T) target_dir=$1; shift;;
    -o) output=$1; shift;;
    --) break;;
  esac
done

frag="${build_dir}/tests/tcg/${target_dir}/config-target.mak"
if ! test -f "$frag"; then
  # No cross-compiler available
  # Copy pre-build image into build tree
  cp "${source_dir}/$(basename ${output})" "${output}"
  exit $?
fi

# Extract cross-compiler from the makefile fragment, and build.
CC=$(grep CC= "$frag" | sed s/CC=//)
exec $CC -o "$output" $@
