.. SPDX-License-Identifier: GPL-2.0-or-later

zlib compression syscall-filter example
=======================================

This directory contains the guest-side pieces used by
``contrib/plugins/syscall_filter_zlib.c``:

* ``zcompress-demo.c`` is linked against ``libdemo-zlib.so`` and calls the
  compression helpers directly.
* The plugin intercepts the loader's ``open()``, ``openat()``, or
  ``openat2()`` call and returns a file descriptor for
  ``./libdemo-zlib-thunk.so`` instead.
* ``zcompress-thunk.c`` exposes a tiny compression API as thin wrappers around
  magic syscalls.
* The plugin filters those magic syscalls and executes the host zlib
  ``compressBound()``, ``compress2()``, and ``uncompress()`` implementations
  directly on guest buffers.
* The example currently supports ``x86_64`` linux-user only. Extending the
  syscall-number table for more targets is straightforward, but is outside the
  scope of this patch.
* To keep the demo small, the plugin assumes ``guest_base == 0`` on a
  little-endian 64-bit host. For this x86_64 linux-user demo, that means guest
  virtual addresses are directly usable as host pointers. In practice that
  means running ``qemu-x86_64`` on a little-endian 64-bit host without forcing
  a nonzero guest base.

Build the guest-side demo with::

  make

Then run it from this directory with QEMU linux-user and the plugin::

  QEMU_BUILD=/path/to/qemu/build
  $QEMU_BUILD/qemu-x86_64 \
    -plugin $QEMU_BUILD/contrib/plugins/libsyscall_filter_zlib.so \
    -d plugin \
    ./zcompress-demo

The build links ``zcompress-demo`` against ``libdemo-zlib-thunk.so`` while
giving that shared object the soname ``libdemo-zlib.so``. Without the plugin,
the program fails at startup because no ``libdemo-zlib.so`` file exists in the
runtime search path. With the plugin, the guest sees a working library load
even though the loader actually receives ``libdemo-zlib-thunk.so``.
