===========
iOS Support
===========

To run qemu on the iOS platform, some modifications were required. Most of the
modifications are conditioned on the ``CONFIG_IOS`` and configuration variable.

Build support
-------------

For the code to compile, certain changes in the block driver and the slirp
driver had to be made. There is no ``system()`` call, so it has been replaced
with an assertion error. There should be no code path that call system() from
iOS.

``ucontext`` support is broken on iOS. The implementation from ``libucontext``
is used instead.

JIT support
-----------

On iOS, allocating RWX pages require special entitlements not usually granted to
apps. However, it is possible to use `bulletproof JIT`_ with a development
certificate. This means that we need to allocate one chunk of memory with RX
permissions and then mirror map the same memory with RW permissions. We generate
code to the mirror mapping and execute the original mapping.

.. _bulletproof JIT: https://www.blackhat.com/docs/us-16/materials/us-16-Krstic.pdf
