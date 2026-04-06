.. SPDX-License-Identifier: GPL-2.0-or-later

Passthrough syscall-filter example
==================================

This directory contains a zlib demo for ``contrib/plugins/passthrough.c``.

* ``libz.so`` is the guest thunk library (GTL). It exports zlib symbols to the
  guest program.
* ``libz_HTL.so`` is the host thunk library (HTL). It links to native zlib.
* ``zlib-demo.c`` calls ``compressBound()``, ``compress2()``, and
  ``uncompress()`` through normal zlib APIs.

The GTL constructor initializes passthrough in three steps:

1. Send a magic syscall to request loading the HTL for ``libz.so``.
2. Send magic syscalls to resolve HTL entry addresses
   (``compressBound_HTL``, ``compress2_HTL``, ``uncompress_HTL``).
3. Cache those function pointers in GTL globals.

The GTL destructor sends a magic syscall to close the loaded HTL handle.

Each GTL wrapper then sends a magic syscall with:

* the cached HTL function pointer,
* a flat ``void *args[]`` payload holding addresses of wrapper-local argument
  variables,
* and a ``void *ret_ptr`` return-storage address.

This example assumes 64-bit little-endian linux-user guests. To keep it small,
it also assumes ``guest_base == 0`` on a little-endian 64-bit host.

Direct execution of host callbacks back into guest translated code is not
covered here. That requires additional target- and ABI-specific handling
beyond this demo.

Build:

.. code-block:: sh

  # 1) Build host-side passthrough pieces in QEMU's Meson tree.
  #    This produces libpassthrough.so and libz_HTL.so.
  ninja -C /path/to/qemu/build contrib-passthrough-examples

  # 2) Build guest-side GTL + demo in this directory.
  #    By default GUEST_CC=cc; override it for cross guest builds.
  make
  # make GUEST_CC=aarch64-linux-gnu-gcc

Run:

.. code-block:: sh

  QEMU_BUILD=/path/to/qemu/build
  HTL_DIR=$QEMU_BUILD/contrib/plugins/passthrough-examples
  $QEMU_BUILD/qemu-x86_64 \
    -E LD_LIBRARY_PATH=$PWD \
    -plugin $QEMU_BUILD/contrib/plugins/libpassthrough.so,htl_dir=$HTL_DIR \
    -d plugin \
    ./zlib-demo
