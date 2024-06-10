# PL011 QEMU Device Model

This library implements a device model for the PrimeCell® UART (PL011)
device in QEMU.

The C bindings were generated for commit `01782d6b29`:

```console
$ git describe 01782d6b29
v9.0.0-769-g01782d6b29
```

with `bindgen`, using this build target:

```console
$ ninja generated.rs
```

## Build static lib

```sh
cargo build --target x86_64-unknown-linux-gnu
```

Replace host target triplet if necessary.

## Generate Rust documentation

To generate docs for this crate, including private items:

```sh
cargo doc --no-deps --document-private-items --target x86_64-unknown-linux-gnu
```

To include direct dependencies like `bilge` (bitmaps for register types):

```sh
cargo tree --depth 1 -e normal --prefix none \
 | cut -d' ' -f1 \
 | xargs printf -- '-p %s\n' \
 | xargs cargo doc --no-deps --document-private-items --target x86_64-unknown-linux-gnu
```
