.. SPDX-License-Identifier: GPL-2.0-or-later

Callback qsort bridge prototype
===============================

This directory contains a prototype for callback-capable local library
interception on ``qemu-x86_64``.

* ``callback-demo.c`` is linked against ``libdemo-callback-qsort.so`` and calls
  ``callback_qsort()`` directly.
* The plugin intercepts the loader's ``openat()`` and returns a file
  descriptor for ``./libdemo-callback-qsort-thunk.so`` instead.
* ``callback-thunk.S`` issues a ``START`` magic syscall for ``qsort()`` and a
  ``RESUME`` magic syscall from a guest return trampoline.
* ``contrib/plugins/syscall_filter_callback_qsort.c`` runs host ``qsort()``
  inside a ``ucontext`` worker, yields on every comparator invocation, and
  redirects control flow into the guest comparator with
  ``qemu_plugin_set_pc()``.
* This callback prototype is intentionally ``x86_64``-only. Supporting
  additional targets would require target-specific register and ABI handling,
  which is beyond the scope of this example.
* The plugin is built only on hosts where ``ucontext`` is available.
* To keep the prototype minimal, it assumes ``guest_base == 0`` on a
  little-endian 64-bit host. For this x86_64 linux-user demo, that means guest
  virtual addresses are directly usable as host pointers. In practice that
  means running ``qemu-x86_64`` on a little-endian 64-bit host without forcing
  a nonzero guest base.

Build the guest-side prototype with::

  make

Then run it from this directory with QEMU linux-user and the plugin::

  QEMU_BUILD=/path/to/qemu/build
  $QEMU_BUILD/qemu-x86_64 \
    -plugin $QEMU_BUILD/contrib/plugins/libsyscall_filter_callback_qsort.so \
    -d plugin \
    ./callback-demo

The build links ``callback-demo`` against ``libdemo-callback-qsort-thunk.so``
while giving that shared object the soname ``libdemo-callback-qsort.so``.
Without the plugin, the program fails at startup because no
``libdemo-callback-qsort.so`` file exists in the runtime search path. With the
plugin, the guest sees a working library load even though the loader actually
receives ``libdemo-callback-qsort-thunk.so``.
