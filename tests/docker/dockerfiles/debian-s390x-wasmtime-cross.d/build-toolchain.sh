#!/bin/sh

# Build the stable wasmtime testsuite and run it with qemu-user from $PATH.
# ".rustup", ".cargo" and "wasmtime" subdirectories will be created or updated
# in the current directory.
#
# Based on https://github.com/bytecodealliance/wasmtime/blob/v0.37.0/.github/workflows/main.yml#L208.
#
# Usage:
#
#     ./test TARGET_ARCH [CARGO_ARGS ...]
#
# where TARGET_ARCH is the architecture to test (aarch64 or s390x) and
# CARGO_ARGS are the extra arguments passed to cargo test.

set -e -u -x

# Dependency versions.
export RUSTUP_TOOLCHAIN=1.62.0

# Bump when https://github.com/bytecodealliance/wasmtime/pull/4377 is
# integrated. Until this moment there will be some unnecessary rebuilds.
wasmtime_version=0.37.0

# Script arguments.
arch=$1
shift
arch_upper=$(echo "$arch" | tr '[:lower:]' '[:upper:]')

# Install/update Rust.
export RUSTUP_HOME="$PWD/.rustup"
export CARGO_HOME="$PWD/.cargo"
curl \
    --proto '=https' \
    --tlsv1.2 \
    -sSf \
    https://sh.rustup.rs \
    | sh -s -- -y \
        --default-toolchain="$RUSTUP_TOOLCHAIN" \
        --target=wasm32-wasi \
        --target=wasm32-unknown-unknown \
        --target="$arch"-unknown-linux-gnu
cat >"$CARGO_HOME/config" <<HERE
[build]
# Save space by not generating data to speed-up delta builds.
incremental = false

[profile.test]
# Save space by not generating debug information.
debug = 0

[net]
# Speed up crates.io index update.
git-fetch-with-cli = true
HERE
. "$PWD/.cargo/env"

# Checkout/update wasmtime.
if [ -d wasmtime ]; then
    cd wasmtime
    git fetch --force --tags
    git checkout v"$wasmtime_version"
    git submodule update --init --recursive
else
    git clone \
        --depth=1 \
        --recurse-submodules \
        --shallow-submodules \
        -b v"$wasmtime_version" \
        https://github.com/bytecodealliance/wasmtime.git
    cd wasmtime
fi

# Run wasmtime tests.
export CARGO_BUILD_TARGET="$arch-unknown-linux-gnu"
runner_var=CARGO_TARGET_${arch_upper}_UNKNOWN_LINUX_GNU_RUNNER
linker_var=CARGO_TARGET_${arch_upper}_UNKNOWN_LINUX_GNU_LINKER
eval "export $runner_var=\"qemu-$arch -L /usr/$arch-linux-gnu\""
eval "export $linker_var=$arch-linux-gnu-gcc"
export CARGO_PROFILE_DEV_OPT_LEVEL=2
export WASMTIME_TEST_NO_HOG_MEMORY=1
export RUST_BACKTRACE=1
ci/run-tests.sh --locked "$@"
