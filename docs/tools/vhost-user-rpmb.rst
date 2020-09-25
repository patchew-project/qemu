QEMU vhost-user-rpmb - rpmb emulation backend
=============================================

Synopsis
--------

**vhost-user-rpmb** [*OPTIONS*]

Description
-----------

This program is a vhost-user backed that emulates a VirtIO Replay
Protected Memory Block device. These are usually special partitions
that are part of a flash device that offer protection against reply
attacks. They are used to store secure information in a way that is
hard to tamper with.

This program is designed to work with QEMU's ``--device
vhost-user-rpmb-pci`` but should work with any virtual machine
monitor (VMM) that supports vhost-user. See the Examples section
below.

This program requires a backing store to persist any data programmed
into the device. The spec supports devices up 32Mb in size. For the
daemon this is simply a raw file of the appropriate size. To program
the device it needs to have a key. This can either be programmed by
the guest at the start or come from a key file supplied to the daemon.

Options
-------

.. program:: vhost-user-rpmb

.. option:: -h, --help

  Print help.

.. option:: -V, --version

  Print version.

.. option:: -v, --verbose

   Increase verbosity of output
            
.. option:: --debug

  Enable debug output.

.. option:: --socket-path=PATH

  Listen on vhost-user UNIX domain socket at PATH. Incompatible with --fd.

.. option:: --fd=FDNUM

  Accept connections from vhost-user UNIX domain socket file descriptor FDNUM.
  The file descriptor must already be listening for connections.
  Incompatible with --socket-path.

.. option:: --flash-path=PATH

  Path to the backing store for the flash image, can be up to 32Mb in size.

.. option:: --key-path=PATH

  Path to the backing store for the key of 32 bytes.
            
.. option:: --key-set

  Treat the value of key-path as set meaning the key cannot be
  reprogrammed by the guest.

.. option:: --initial-counter=N

  Set the initial value of the devices write count. It is
  incremented by each write operation. 

Examples
--------

The daemon should be started first:

::

  host# vhost-user-rpmb --socket-path=vrpmb.sock \
   --flash-path=flash.img \
   --key-path=key --key-set \
   --initial-counter=1234

The QEMU invocation needs to create a chardev socket the device can
use to communicate as well as share the guests memory over a memfd.

::

  host# qemu-system \
      -chardev socket,path=vrpmb.sock,id=vrpmb \
      -device vhost-user-rpmb-pci,chardev=vrpmb,id=rpmb \
      -m 4096 \
      -object memory-backend-file,id=mem,size=4G,mem-path=/dev/shm,share=on \
      -numa node,memdev=mem \
      ...

