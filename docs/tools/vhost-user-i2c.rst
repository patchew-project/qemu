QEMU vhost-user-i2c - I2C emulation backend
===========================================

Synopsis
--------

**vhost-user-i2c** [*OPTIONS*]

Description
-----------

This program is a vhost-user backend that emulates a VirtIO I2C bus.
This program takes the layout of the i2c bus and its devices on the host
OS and then talks to them via the /dev/i2c-X interface when a request
comes from the guest OS for an I2C device.

This program is designed to work with QEMU's ``-device
vhost-user-i2c-pci`` but should work with any virtual machine monitor
(VMM) that supports vhost-user. See the Examples section below.

Options
-------

.. program:: vhost-user-i2c

.. option:: -h, --help

  Print help.

.. option:: -v, --verbose

   Increase verbosity of output

.. option:: -s, --socket-path=PATH

  Listen on vhost-user UNIX domain socket at PATH. Incompatible with --fd.

.. option:: -f, --fd=FDNUM

  Accept connections from vhost-user UNIX domain socket file descriptor FDNUM.
  The file descriptor must already be listening for connections.
  Incompatible with --socket-path.

.. option:: -l, --device-list=I2C-DEVICES

  I2c device list at the host OS in the format:
      <bus>:<client_addr>[:<client_addr>],[<bus>:<client_addr>[:<client_addr>]]

      Example: --device-list "2:1c:20,3:10:2c"

  Here,
      bus (decimal): adatper bus number. e.g. 2 for /dev/i2c-2, 3 for /dev/i2c-3.
      client_addr (hex): address for client device. e.g. 0x1C, 0x20, 0x10, 0x2C.

Examples
--------

The daemon should be started first:

::

  host# vhost-user-i2c --socket-path=vi2c.sock --device-list 0:20

The QEMU invocation needs to create a chardev socket the device can
use to communicate as well as share the guests memory over a memfd.

::

  host# qemu-system \
      -chardev socket,path=vi2c.sock,id=vi2c \
      -device vhost-user-i2c-pci,chardev=vi2c,id=i2c \
      -m 4096 \
      -object memory-backend-file,id=mem,size=4G,mem-path=/dev/shm,share=on \
      -numa node,memdev=mem \
      ...
