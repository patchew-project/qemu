#!/bin/sh

export CARGO_CMD="$1"
shift

if [ "$CARGO_CMD" = "build" ]; then
    export MESON_BUILD_TYPE="$1"
    shift
    export MESON_CURRENT_BUILD_DIR="$1"
    shift
    export MESON_SOURCE_ROOT="$1"
    shift
    export MESON_BUILD_ROOT="$1"
    shift
fi

OUTDIR=debug

if [[ "$MESON_BUILD_TYPE" = release ]]
then
    EXTRA_ARGS="--release"
    OUTDIR=release
fi

cargo "$CARGO_CMD" --manifest-path "$MESON_SOURCE_ROOT/Cargo.toml" --target-dir="$MESON_BUILD_ROOT/rs-target" $EXTRA_ARGS "$@"

if [ "$CARGO_CMD" = "build" ]; then
    touch "$MESON_CURRENT_BUILD_DIR"/cargo-build.stamp
fi
