===========
iOS Support
===========

To run qemu on the iOS platform, some modifications were required. Most of the
modifications are conditioned on the ``CONFIG_IOS`` and ``CONFIG_IOS_JIT``
configuration variables.

Build support
-------------

For the code to compile, certain changes in the block driver and the slirp
driver had to be made. There is no ``system()`` call, so code requiring it had
to be disabled.

``ucontext`` support is broken on iOS. The implementation from ``libucontext``
is used instead.

Because ``fork()`` is not allowed on iOS apps, the option to build qemu and the
utilities as shared libraries is added. Note that because qemu does not perform
resource cleanup in most cases (open files, allocated memory, etc), it is
advisable that the user implements a proxy layer for syscalls so resources can
be kept track by the app that uses qemu as a shared library.

JIT support
-----------

On iOS, allocating RWX pages require special entitlements not usually granted to
apps. However, it is possible to use `bulletproof JIT`_ with a development
certificate. This means that we need to allocate one chunk of memory with RX
permissions and then mirror map the same memory with RW permissions. We generate
code to the mirror mapping and execute the original mapping.

With ``CONFIG_IOS_JIT`` defined, we store inside the TCG context the difference
between the two mappings. Then, we make sure that any writes to JIT memory is
done to the pointer + the difference (in order to get a pointer to the mirror
mapped space). Additionally, we make sure to flush the data cache before we
invalidate the instruction cache so the changes are seen in both mappings.

.. _bulletproof JIT: https://www.blackhat.com/docs/us-16/materials/us-16-Krstic.pdf
