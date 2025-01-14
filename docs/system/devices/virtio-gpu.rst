..
   SPDX-License-Identifier: GPL-2.0-or-later

virtio-gpu
==========

The virtio-gpu device provides a GPU and display controller
paravirtualized using VirtIO. It supports a number of different modes
from simple 2D displays to fully accelerated 3D graphics.

Dependencies
............

.. note::
  GPU virtualisation is still an evolving field. Depending on the mode
  you are running you may need to override distribution supplied
  libraries with more recent versions or enable build options.

Host requirements
-----------------

Depending on the mode there are a number of requirements the host must
meet to be able to be able to support guests. For 3D acceleration QEMU
must be able to access the hosts GPU and for the best performance be
able to reliably share GPU memory with the guest.

.. list-table:: Host Requirements
  :header-rows: 1

  * - Mode
    - Kernel
    - Userspace
  * - virtio-gpu
    - framebuffer enabled
    - GTK or SDL display
  * - virtio-gpu-gl (OpenGL pass-through)
    - GPU enabled
    - libvirglrenderer (virgl support)
  * - virtio-gpu-gl (rutabaga/gfxstream)
    - GPU enabled
    - aemu/rutabaga_gfx_ffi or vhost-user client with support
  * - virtio-gpu-gl (Vulkan pass-through)
    - Linux 6.13, CONFIG_UDMA
    - libvirglrenderer (>= 1.1.0, venus support)
  * - virtio-gpu-gl (Native Context)
    - Linux 6.13, CONFIG_UDMA
    - libvirglrenderer (master, possibly with patches)


Guest requirements
------------------

virtio-gpu requires a guest Linux kernel built with the
``CONFIG_DRM_VIRTIO_GPU`` option. Otherwise for 3D accelerations you
will need support from Mesa configured for whichever encapsulation you
need.

.. list-table:: Guest Requirements
  :header-rows: 1

  * - Mode
    - Min Mesa Version
    - Mesa flags
  * - virtio-gpu
    - n/a
    - n/a
  * - virtio-gpu-gl (OpenGL pass-through)
    - 20.3.0+
    - -Dgallium-drivers=virgl
  * - virtio-gpu-gl (rutabaga/gfxstream)
    - 24.3.0+
    - -Dvulkan-drivers=gfxstream
  * - virtio-gpu-gl (Vulkan pass-through)
    - 24.2+
    - -Dvulkan-drivers=virtio
  * - virtio-gpu-gl (Native Context/Freedreno)
    - master
    - ?
  * - virtio-gpu-gl (Native Context/Intel)
    - `mr29870`_
    - -Dvulkan-drivers=iris -Dintel-virtio-experimental=true
  * - virtio-gpu-gl (Native Context/AMD)
    - `mr21658`_
    - -Dvulkan-drivers=radeonsi

.. _mr29870: https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/29870
.. _mr21658: https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21658
      
Further information
...................


QEMU virtio-gpu variants
------------------------

QEMU virtio-gpu device variants come in the following form:

 * ``virtio-vga[-BACKEND]``
 * ``virtio-gpu[-BACKEND][-INTERFACE]``
 * ``vhost-user-vga``
 * ``vhost-user-pci``

**Backends:** QEMU provides a 2D virtio-gpu backend, and two accelerated
backends: virglrenderer ('gl' device label) and rutabaga_gfx ('rutabaga'
device label).  There is a vhost-user backend that runs the graphics stack
in a separate process for improved isolation.

**Interfaces:** QEMU further categorizes virtio-gpu device variants based
on the interface exposed to the guest. The interfaces can be classified
into VGA and non-VGA variants. The VGA ones are prefixed with virtio-vga
or vhost-user-vga while the non-VGA ones are prefixed with virtio-gpu or
vhost-user-gpu.

The VGA ones always use the PCI interface, but for the non-VGA ones, the
user can further pick between MMIO or PCI. For MMIO, the user can suffix
the device name with -device, though vhost-user-gpu does not support MMIO.
For PCI, the user can suffix it with -pci. Without these suffixes, the
platform default will be chosen.

virtio-gpu 2d
-------------

The default 2D backend only performs 2D operations. The guest needs to
employ a software renderer for 3D graphics.

Typically, the software renderer is provided by `Mesa`_ or `SwiftShader`_.
Mesa's implementations (LLVMpipe, Lavapipe and virgl below) work out of box
on typical modern Linux distributions.

.. parsed-literal::
    -device virtio-gpu

.. _Mesa: https://www.mesa3d.org/
.. _SwiftShader: https://github.com/google/swiftshader

virtio-gpu virglrenderer
------------------------

When using virgl accelerated graphics mode in the guest, OpenGL API calls
are translated into an intermediate representation (see `Gallium3D`_). The
intermediate representation is communicated to the host and the
`virglrenderer`_ library on the host translates the intermediate
representation back to OpenGL API calls.

.. parsed-literal::
    -device virtio-gpu-gl

.. _Gallium3D: https://www.freedesktop.org/wiki/Software/gallium/
.. _virglrenderer: https://gitlab.freedesktop.org/virgl/virglrenderer/

Translation of Vulkan API calls is supported since release of `virglrenderer`_
v1.0.0 using `venus`_ protocol. ``Venus`` virtio-gpu capability set ("capset")
requires host blob support (``hostmem`` and ``blob`` fields) and should
be enabled using ``venus`` field. The ``hostmem`` field specifies the size
of virtio-gpu host memory window. This is typically between 256M and 8G.

.. parsed-literal::
    -device virtio-gpu-gl,hostmem=8G,blob=true,venus=true

.. _venus: https://gitlab.freedesktop.org/virgl/venus-protocol/

virtio-gpu rutabaga
-------------------

virtio-gpu can also leverage rutabaga_gfx to provide `gfxstream`_
rendering and `Wayland display passthrough`_.  With the gfxstream rendering
mode, GLES and Vulkan calls are forwarded to the host with minimal
modification.

The crosvm book provides directions on how to build a `gfxstream-enabled
rutabaga`_ and launch a `guest Wayland proxy`_.

This device does require host blob support (``hostmem`` field below). The
``hostmem`` field specifies the size of virtio-gpu host memory window.
This is typically between 256M and 8G.

At least one virtio-gpu capability set ("capset") must be specified when
starting the device.  The currently capsets supported are ``gfxstream-vulkan``
and ``cross-domain`` for Linux guests. For Android guests, the experimental
``x-gfxstream-gles`` and ``x-gfxstream-composer`` capsets are also supported.

The device will try to auto-detect the wayland socket path if the
``cross-domain`` capset name is set.  The user may optionally specify
``wayland-socket-path`` for non-standard paths.

The ``wsi`` option can be set to ``surfaceless`` or ``headless``.
Surfaceless doesn't create a native window surface, but does copy from the
render target to the Pixman buffer if a virtio-gpu 2D hypercall is issued.
Headless is like surfaceless, but doesn't copy to the Pixman buffer.
Surfaceless is the default if ``wsi`` is not specified.

.. parsed-literal::
    -device virtio-gpu-rutabaga,gfxstream-vulkan=on,cross-domain=on,
       hostmem=8G,wayland-socket-path=/tmp/nonstandard/mock_wayland.sock,
       wsi=headless

.. _gfxstream: https://android.googlesource.com/platform/hardware/google/gfxstream/
.. _Wayland display passthrough: https://www.youtube.com/watch?v=OZJiHMtIQ2M
.. _gfxstream-enabled rutabaga: https://crosvm.dev/book/appendix/rutabaga_gfx.html
.. _guest Wayland proxy: https://crosvm.dev/book/devices/wayland.html
