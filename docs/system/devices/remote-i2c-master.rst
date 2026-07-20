Remote I2C master
=================

Overview
--------

The Remote I2C master exposes a QEMU I2C bus to the host system through a
FUSE/CUSE (Character device in Userspace) character device. It allows
userspace programs and scripts on the host to interact with I2C slaves
emulated inside QEMU as if they were real hardware devices attached to the
host, accessible with the standard Linux I2C interface and tools such as
``i2c-tools``.

Features
--------

- Virtual I2C controller exposed to the host as a character device via CUSE
- Implements the Linux I2C ioctl interface (``I2C_RDWR``, ``I2C_SMBUS``,
  ``I2C_SLAVE``)
- Supports standard I2C and SMBus protocols
- SMBus "Repeated Start" for atomic write-then-read operations
- Asynchronous, non-blocking transactions driven by QEMU's Bottom Halves (BH)
- Clock-stretching emulation for asynchronous slave devices, so QEMU's main
  loop is never blocked during long transfers
- Integration with QEMU's AioContext for asynchronous I/O
- Debugging support through FUSE debug mode

Architecture
------------

The device is split into three decoupled layers:

Master frontend (FSM)
~~~~~~~~~~~~~~~~~~~~~~~

A non-blocking finite state machine drives the QEMU I2C master. It is pumped
by a QEMU Bottom Half and uses virtual timers to yield during long transfers
and to model clock stretching for asynchronous slaves. The transaction walks
the following states::

    IDLE -> ADDR -> SEND/RECV -> WAIT_STRETCH -> END -> FINISHED

- ``IDLE``         : Resting state; awaits a backend dispatch, checks bus
  busyness and handles retry timers if arbitration was lost.
- ``ADDR``         : Asserts the bus and sends the slave address. A NACK
  aborts the transaction (``ENXIO``); an ACK transitions to SEND, RECV or
  WAIT_STRETCH.
- ``SEND``         : Pushes data bytes to the bus, synchronously in a loop or
  one byte at a time for async slaves.
- ``RECV``         : Reads data bytes from the bus, with the same yielding
  behaviour as SEND.
- ``WAIT_STRETCH`` : Yields back to QEMU to simulate clock stretching or to
  enforce an artificial delay; on timer expiry it bounces back to SEND/RECV.
- ``END``          : Transitional state that guarantees ``i2c_end_transfer``
  is called gracefully.
- ``FINISHED``     : Cleans up timers, releases the bus and invokes the
  backend completion callbacks.

Error handling covers NACKs (``ENXIO``), lost arbitration (``EBUSY``, with an
optional back-off and retry cooldown), stretch timeouts to avoid hung
transactions, and manual abort/reset issued by the backend.

Abstract backend
~~~~~~~~~~~~~~~~~

An abstract ``RemoteI2CBackend`` QOM base class strictly decouples the
internal QEMU I2C hardware state machine (the frontend) from any
host-specific transport layer. It exposes the ``on_tx_complete`` and
``on_tx_error`` virtual callbacks used by the FSM to report results.

CUSE backend
~~~~~~~~~~~~

The concrete ``remote-i2c-backend-cuse`` backend implements the transport
over CUSE. It manages the FUSE/CUSE session, integrating its file descriptors
directly into QEMU's main AioContext event loop. It translates Linux
user-space ioctls (``I2C_RDWR``, ``I2C_SMBUS``, ``I2C_SLAVE``) into generic
byte streams for the master frontend to process, and formats QEMU's response
data back into Linux-compatible I2C/SMBus structures to reply to the FUSE
driver.

Concurrent bus access
~~~~~~~~~~~~~~~~~~~~~

The remote master and the emulated CPU may both attempt to drive the same I2C
bus simultaneously, mirroring the arbitration behaviour of a real multi-master
I2C bus. QEMU's I2C core serializes ownership through ``i2c_bus_master()``,
which returns an error when the bus is already held by another master.

The remote master handles this in one of two ways, controlled by the
``raise-arbitrage-lost`` property:

- **Retry with back-off** (default, ``raise-arbitrage-lost=false``): the
  master treats a busy bus as a lost-arbitration event, backs off, and
  retries the transaction after a configurable delay. This matches the
  behaviour of hardware multi-master I2C controllers.

- **Fail immediately** (``raise-arbitrage-lost=true``): the master surfaces
  ``EBUSY`` to the caller at once without retrying. Use this when the host
  test suite must detect contention explicitly rather than wait for it to
  resolve.

Invocation
----------

The backend can be wired implicitly through the master device::

    -object remote-i2c-backend-cuse,id=<id>,devname=<node_name>
    -device remote-i2c-master,i2cbus=<bus>,backend=<id>

That creates a character device named ``<node_name>`` (for example
``/dev/i2c-33``) on the host.

Requirements
------------

Kernel requirements
~~~~~~~~~~~~~~~~~~~~~

- CUSE module loaded: ``sudo modprobe cuse``
- FUSE support enabled

Library dependencies
~~~~~~~~~~~~~~~~~~~~~~

- libfuse3 or libfuse (version 2.9.0 or higher)
- FUSE development headers

Debugging
---------

FUSE debug output can be enabled by passing FUSE options to the CUSE backend
through ``fuse-opts`` or by ``debug=true`` for short.

.. code-block:: bash

    -object remote-i2c-backend-cuse,id=b0,devname=i2c-33,fuse-opts=-d

Running the FUSE session in the foreground with debug enabled prints the
incoming CUSE/ioctl traffic, which is useful when diagnosing transport
issues.

Limitations
-----------

10-bit I2C addressing
~~~~~~~~~~~~~~~~~~~~~

Only 7-bit I2C addresses (0–127) are supported. QEMU's I2C core stores slave
addresses as ``uint8_t`` and all bus functions (``i2c_start_send``,
``i2c_start_recv``, ``i2c_scan_bus``) accept ``uint8_t address``, so 10-bit
addressing cannot be represented at the framework level. The CUSE backend
reflects this by rejecting any address outside the 0–127 range with
``EINVAL``. The ``I2C_M_TEN`` flag in ``I2C_RDWR`` messages is ignored.

Troubleshooting
---------------

CUSE_INIT failures
~~~~~~~~~~~~~~~~~~~

If you encounter ``CUSE_INIT`` errors:

1. Verify the CUSE module is loaded:

   .. code-block:: bash

       lsmod | grep cuse
       sudo modprobe cuse

2. Check permissions on the CUSE control device:

   .. code-block:: bash

       # Ensure the user has access to /dev/cuse
       ls -la /dev/cuse

Examples
--------

Basic usage
~~~~~~~~~~~

Start QEMU with a ``tmp105`` temperature sensor on an Aspeed I2C bus and
expose that bus to the host as ``/dev/i2c-33``:

.. code-block:: bash

    ./qemu-system-arm -M ast2600-evb \
        -device tmp105,address=0x40,bus=aspeed.i2c.bus.0 \
        -device remote-i2c-master,i2cbus=aspeed.i2c.bus.0,devname=i2c-33

Then access the emulated sensor from the host with ``i2c-tools``:

.. code-block:: console

    $ i2cdetect -y 33
         0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
    00:                         -- -- -- -- -- -- -- --
    10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
    20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
    30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
    40: 40 -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
    50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
    60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

    $ i2cget -y 33 0x40 0x2
    0x4b

    $ i2cget -y 33 0x40 0x3
    0x50

See also
--------

- `FUSE Documentation <https://github.com/libfuse/libfuse>`_
- `Character devices in user space <https://lwn.net/Articles/308445/>`_
