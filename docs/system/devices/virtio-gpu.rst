..
   SPDX-License-Identifier: GPL-2.0

virtio-gpu
==========

This document explains the setup and usage of the virtio-gpu device.
The virtio-gpu device paravirtualizes the GPU and display controller.

Linux kernel support
--------------------

virtio-gpu requires a guest Linux kernel built with the
``CONFIG_DRM_VIRTIO_GPU`` option.

QEMU virtio-gpu variants
------------------------

QEMU provides a 2D virtio-gpu backend, and two accelerated backends:
virglrenderer ('gl' device label) and rutabaga_gfx ('rutabaga' device
label).  There is a vhost-user backend that runs the graphics stack in
a separate process for improved isolation.

Theses backends can be further classified into VGA and non-VGA variants.
The VGA ones are prefixed with virtio-vga or vhost-user-vga while the
non-VGA ones are prefixed with virtio-gpu or vhost-user-gpu.

The VGA ones always use PCI interface, but for the non-VGA ones, you can
further pick simple MMIO or PCI. For MMIO, you can suffix the device
name with -device though vhost-user-gpu apparently does not support
MMIO. For PCI, you can suffix it with -pci. Without these suffixes, the
platform default will be chosen.  The syntax of  available combinations
is listed below.

 * ``virtio-vga[-BACKEND]``
 * ``virtio-gpu[-BACKEND][-INTERFACE]``
 * ``vhost-user-vga``
 * ``vhost-user-pci``

This document uses the PCI variant in examples.

virtio-gpu 2d
-------------

The default 2D backend only performs 2D operations. The guest needs to
employ a software renderer for 3D graphics.

Typically, the software renderer is provided by `Mesa`_ or `SwiftShader`_.
Mesa's implementations (LLVMpipe, Lavapipe and virgl below) work out of box
on typical modern Linux distributions.

.. parsed-literal::
    -device virtio-gpu-pci

.. _Mesa: https://www.mesa3d.org/
.. _SwiftShader: https://github.com/google/swiftshader

virtio-gpu virglrenderer
------------------------

When using virgl accelerated graphics mode, OpenGL API calls are translated
into an intermediate representation (see `Gallium3D`_). The intermediate
representation is communicated to the host and the `virglrenderer`_ library
on the host translates the intermediate representation back to OpenGL API
calls.

.. parsed-literal::
    -device virtio-gpu-gl-pci

.. _Gallium3D: https://www.freedesktop.org/wiki/Software/gallium/
.. _virglrenderer: https://gitlab.freedesktop.org/virgl/virglrenderer/

virtio-gpu rutabaga
-------------------

virtio-gpu can also leverage `rutabaga_gfx`_ to provide `gfxstream`_ rendering
and `Wayland display passthrough`_.  With the gfxstream rendering mode, GLES
and Vulkan calls are forwarded directly to the host with minimal modification.

The crosvm book provides directions on how to build a `gfxstream-enabled
rutabaga`_ and launch a `guest Wayland compositor`_.

This device does require host blob support (``hostmem`` field below), but not
all capsets (``capset_names`` below) have to enabled when starting the device.

The currently supported ``capset_names`` are ``gfxstream-vulkan`` and
``cross-domain`` on Linux guests.  For Android guests, ``gfxstream-gles`` is
also supported.

.. parsed-literal::
    -device virtio-gpu-rutabaga-pci,capset_names=gfxstream-vulkan:cross-domain,\\
      hostmem=8G,wayland_socket_path="$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY"

.. _rutabaga_gfx: https://github.com/google/crosvm/blob/main/rutabaga_gfx/ffi/src/include/rutabaga_gfx_ffi.h
.. _gfxstream: https://android.googlesource.com/platform/hardware/google/gfxstream/
.. _Wayland display passthrough: https://www.youtube.com/watch?v=OZJiHMtIQ2M
.. _gfxstream-enabled rutabaga: https://crosvm.dev/book/appendix/rutabaga_gfx.html
.. _guest Wayland compositor: https://crosvm.dev/book/devices/wayland.html
