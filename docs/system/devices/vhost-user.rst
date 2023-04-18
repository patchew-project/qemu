.. _vhost_user:

vhost-user back ends
--------------------

vhost-user back ends are way to service the request of VirtIO devices
outside of QEMU itself. To do this there are a number of things
required.

vhost-user device
===================

These are simple stub devices that ensure the VirtIO device is visible
to the guest. The code is mostly boilerplate although each device has
a ``chardev`` option which specifies the ID of the ``--chardev``
device that connects via a socket to the vhost-user *daemon*.

Each device will have an virtio-mmio and virtio-pci variant. See your
platform details for what sort of virtio bus to use.

.. list-table:: vhost-user devices
  :widths: 20 20 60
  :header-rows: 1

  * - Device
    - Type
    - Notes
  * - vhost-user-device
    - Generic Development Device
    - You must manually specify ``virtio-id`` and the correct ``num_vqs``. Intended for expert use.
  * - vhost-user-blk
    - Block storage
    -
  * - vhost-user-fs
    - File based storage driver
    - See https://gitlab.com/virtio-fs/virtiofsd
  * - vhost-user-scsi
    - SCSI based storage
    - See contrib/vhost-user/scsi
  * - vhost-user-gpio
    - Proxy gpio pins to host
    - See https://github.com/rust-vmm/vhost-device
  * - vhost-user-i2c
    - Proxy i2c devices to host
    - See https://github.com/rust-vmm/vhost-device
  * - vhost-user-input
    - Generic input driver
    - See contrib/vhost-user-input
  * - vhost-user-rng
    - Entropy driver
    - :ref:`vhost_user_rng`
  * - vhost-user-gpu
    - GPU driver
    -
  * - vhost-user-vsock
    - Socket based communication
    - See https://github.com/rust-vmm/vhost-device

vhost-user daemon
=================

This is a separate process that is connected to by QEMU via a socket
following the :ref:`vhost_user_proto`. There are a number of daemons
that can be built when enabled by the project although any daemon that
meets the specification for a given device can be used.

Shared memory object
====================

In order for the daemon to access the VirtIO queues to process the
requests it needs access to the guest's address space. This is
achieved via the ``memory-backend-file`` or ``memory-backend-memfd``
objects. A reference to a file-descriptor which can access this object
will be passed via the socket as part of the protocol negotiation.

Currently the shared memory object needs to match the size of the main
system memory as defined by the ``-m`` argument.

Example
=======

First start you daemon.

.. parsed-literal::

  $ virtio-foo --socket-path=/var/run/foo.sock $OTHER_ARGS

The you start your QEMU instance specifying the device, chardev and
memory objects.

.. parsed-literal::

  $ |qemu_system| \\
      -m 4096 \\
      -chardev socket,id=ba1,path=/var/run/foo.sock \\
      -device vhost-user-foo,chardev=ba1,$OTHER_ARGS \\
      -object memory-backend-memfd,id=mem,size=4G,share=on \\
      -numa node,memdev=mem \\
        ...

