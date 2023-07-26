.. SPDX-License-Identifier: GPL-2.0-or-later

====================
Cross-compiling QEMU
====================

Cross-compiling QEMU first requires the preparation of a cross-toolchain
and the cross-compiling of QEMU's dependencies. While the steps will be
similar across architectures, each architecture will have its own specific
recommendations. This document collects architecture-specific procedures
and hints that may be used to cross-compile QEMU, where typically the host
environment is x86.

RISC-V
======

Toolchain
---------

Select a root directory for the cross environment
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Export an environment variable pointing to a root directory
for the cross environment. For example, ::

  $ export PREFIX="$HOME/opt/riscv"

Create a work directory
^^^^^^^^^^^^^^^^^^^^^^^

Tools and several components will need to be downloaded and built. Create
a directory for all the work, ::

  $ export WORK_DIR="$HOME/work/xqemu"
  $ mkdir -p "$WORK_DIR"

Select and prepare the toolchain
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Select a toolchain such as [riscv-toolchain]_ and follow its instructions
for building and installing it to ``$PREFIX``, e.g. ::

  $ cd "$WORK_DIR"
  $ git clone https://github.com/riscv/riscv-gnu-toolchain
  $ cd riscv-gnu-toolchain
  $ ./configure --prefix="$PREFIX"
  $ make -j$(nproc) linux

Set the ``$CROSS_COMPILE`` environment variable to the prefix of the cross
tools and add the tools to ``$PATH``, ::

$ export CROSS_COMPILE=riscv64-unknown-linux-gnu-
$ export PATH="$PREFIX/bin:$PATH"

Also set ``$SYSROOT``, where all QEMU cross-compiled dependencies will be
installed. The toolchain installation likely created a 'sysroot' directory
at ``$PREFIX/sysroot``, which is the default location for most cross
tools, making it a good location, ::

  $ mkdir -p "$PREFIX/sysroot"
  $ export SYSROOT="$PREFIX/sysroot"

Create a pkg-config wrapper
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The build processes of QEMU and some of its dependencies depend on
pkg-config. Create a wrapper script for it which works for the cross
environment: ::

  $ cat <<EOF >"$PREFIX/bin/${CROSS_COMPILE}pkg-config"
  #!/bin/sh

  [ "\$SYSROOT" ] || exit 1

  export PKG_CONFIG_PATH=
  export PKG_CONFIG_LIBDIR="\${SYSROOT}/usr/lib/pkgconfig:\${SYSROOT}/usr/lib64/pkgconfig:\${SYSROOT}/usr/share/pkgconfig"

  exec pkg-config "\$@"
  EOF
  $ chmod +x "$PREFIX/bin/${CROSS_COMPILE}pkg-config"

Create a cross-file for meson builds
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

meson setup, used by some of QEMU's dependencies, needs a "cross-file" to
configure the cross environment. Create one, ::

  $ cd "$WORK_DIR"
  $ cat <<EOF >cross_file.txt
  [host_machine]
  system = 'linux'
  cpu_family = 'riscv64'
  cpu = 'riscv64'
  endian = 'little'

  [binaries]
  c = '${CROSS_COMPILE}gcc'
  cpp = '${CROSS_COMPILE}g++'
  ar = '${CROSS_COMPILE}ar'
  ld = '${CROSS_COMPILE}ld'
  objcopy = '${CROSS_COMPILE}objcopy'
  strip = '${CROSS_COMPILE}strip'
  pkgconfig = '${CROSS_COMPILE}pkg-config'
  EOF

Cross-compile dependencies
--------------------------

glibc
^^^^^

If [riscv-toolchain]_ was selected for the toolchain then this step is
already complete and glibc has already been installed into ``$SYSROOT``.
Otherwise, cross-compile glibc and install it to ``$SYSROOT``.

libffi
^^^^^^

::

  $ cd "$WORK_DIR"
  $ git clone https://gitlab.freedesktop.org/gstreamer/meson-ports/libffi.git
  $ cd libffi
  $ meson setup --cross-file ../cross_file.txt --prefix="$SYSROOT/usr" _build
  $ ninja -C _build
  $ ninja -C _build install

*Building libffi seperately avoids a compilation error generated when
building it as a subproject of glib.*

glib
^^^^

::

  $ cd "$WORK_DIR"
  $ git clone https://github.com/GNOME/glib.git
  $ cd glib
  $ meson setup --cross-file ../cross_file.txt --prefix="$SYSROOT/usr" _build
  $ ninja -C _build
  $ ninja -C _build install

libslirp [optional]
^^^^^^^^^^^^^^^^^^^

::

  $ cd "$WORK_DIR"
  $ git clone https://gitlab.com/qemu-project/libslirp.git
  $ cd libslirp
  $ meson setup --cross-file ../cross_file.txt --prefix="$SYSROOT/usr" _build
  $ ninja -C _build
  $ ninja -C _build install

pixman
^^^^^^

First ensure the 'libtool' package is installed, e.g.
``sudo dnf install libtool`` or ``sudo apt install libtool``

::

  $ cd "$WORK_DIR"
  $ git clone https://gitlab.freedesktop.org/pixman/pixman
  $ cd pixman
  $ ./autogen.sh
  $ ./configure --prefix="$SYSROOT/usr" --host=riscv64-unknown-linux-gnu
  $ make -j$(nproc)
  $ make install

Cross-compile QEMU
------------------

::

  $ cd "$WORK_DIR"
  $ git clone https://gitlab.com/qemu-project/qemu.git
  $ cd qemu
  $ mkdir -p build/install_dir
  $ cd build
  $ ../configure --target-list=riscv64-softmmu --cross-prefix=$CROSS_COMPILE --prefix="$PWD/install_dir"
  $ make -j$(nproc)
  $ make install

*Cross-compiling QEMU with different configurations may require more
dependencies to be built and installed in the sysroot.*

Running QEMU
------------

``build/install_dir`` may now be copied to the target and its bin
directory may be added to the target user's PATH. Prior to running
QEMU, ensure all the libraries it depends on are present, ::

  $ ldd /path/to/bin/qemu-system-riscv64

For example, it may necessary to install zlib libraries, e.g.
``sudo dnf install zlib-devel`` or ``sudo apt install zlib1g-dev``

Subsequent QEMU Cross-compiling
-------------------------------

Unless it's necessary to update and recompile the toolchain or
dependencies, then most steps do not need to be repeated for subsequent
compiles. Simply ensure the toolchain is in ``$PATH``, ``$SYSROOT`` points
at the sysroot, and then follow the QEMU cross-compile steps in
"Cross-compile QEMU". For example, ::

  $ export PATH="$HOME/opt/riscv/bin:$PATH"
  $ export SYSROOT="$HOME/opt/riscv/sysroot"
  $ cd /path/to/qemu
  $ mkdir -p build/install_dir
  $ cd build
  $ ../configure --target-list=riscv64-softmmu --cross-prefix=riscv64-unknown-linux-gnu- --prefix="$PWD/install_dir"
  $ make -j
  $ make install

References
----------

.. [riscv-toolchain] https://github.com/riscv/riscv-gnu-toolchain
