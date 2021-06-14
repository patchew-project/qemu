QEMU vhost-user-rng - RNG emulation backend
===========================================

Synopsis
--------

**vhost-user-rng** [*OPTIONS*]

Description
-----------

This program is a vhost-user backend that emulates a VirtIO random number
generator (RNG).  It uses the host's random number generator pool,
/dev/urandom by default but configurable at will, to satisfy requests from
guests.

This program is designed to work with QEMU's ``-device
vhost-user-rng-pci`` but should work with any virtual machine monitor
(VMM) that supports vhost-user. See the Examples section below.

Options
-------

.. program:: vhost-user-rng

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

.. option:: -p, --period

  Rate, in milliseconds, at which the RNG hardware can generate random data.
  Used in conjunction with the --max-bytes option.

.. option:: -m, --max-bytes

  In conjuction with the --period parameter, provides the maximum number of byte
  per milliseconds a RNG device can generate.

Examples
--------

The daemon should be started first:

::

  host# vhost-user-rng --socket-path=rng.sock --period=1000 --max-bytes=4096

The QEMU invocation needs to create a chardev socket the device can
use to communicate as well as share the guests memory over a memfd.

::

  host# qemu-system								\
      -chardev socket,path=$(PATH)/rng.sock,id=rng0				\
      -device vhost-user-rng-pci,chardev=rng0					\
      -m 4096 									\
      -object memory-backend-file,id=mem,size=4G,mem-path=/dev/shm,share=on	\
      -numa node,memdev=mem							\
      ...
