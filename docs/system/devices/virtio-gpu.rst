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

There are many virtio-gpu device variants, listed below:

 * ``virtio-vga``
 * ``virtio-gpu-pci``
 * ``virtio-vga-gl``
 * ``virtio-gpu-gl-pci``
 * ``virtio-vga-rutabaga``
 * ``virtio-gpu-rutabaga-pci``
 * ``vhost-user-vga``
 * ``vhost-user-gl-pci``

QEMU provides a 2D virtio-gpu backend, and two accelerated backends:
virglrenderer ('gl' device label) and rutabaga_gfx ('rutabaga' device
label).  There is also a vhost-user backend that runs the 2D device
in a separate process.  Each device type as VGA or PCI variant.  This
document uses the PCI variant in examples.

virtio-gpu 2d
-------------

The default 2D mode uses a guest software renderer (llvmpipe, lavapipe,
Swiftshader) to provide the OpenGL/Vulkan implementations.

.. parsed-literal::
    -device virtio-gpu-pci

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

Please refer the `crosvm book`_ on how to setup the guest for Wayland
passthrough (QEMU uses the same implementation).

This device does require host blob support (``hostmem`` field below), but not
all capsets (``capset_names`` below) have to enabled when starting the device.

.. parsed-literal::
    -device virtio-gpu-rutabaga-pci,capset_names=gfxstream-vulkan:cross-domain,\\
      hostmem=8G,wayland_socket_path="$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY"

.. _rutabaga_gfx: https://github.com/google/crosvm/blob/main/rutabaga_gfx/ffi/src/include/rutabaga_gfx_ffi.h
.. _gfxstream: https://android.googlesource.com/platform/hardware/google/gfxstream/
.. _Wayland display passthrough: https://www.youtube.com/watch?v=OZJiHMtIQ2M
.. _crosvm book: https://crosvm.dev/book/devices/wayland.html
