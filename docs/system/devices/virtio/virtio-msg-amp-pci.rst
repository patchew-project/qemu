Virtio-msg AMP PCI
==================

This document explains the setup and usage of the virtio-msg-amp-pci device.
The virtio-msg-amp-pci is an emulated PCI device that provides a small
set of features to enable virtio-msg over shared-memory queues.

Overview
--------

Virtio-msg is a message-based virtio transport: instead of MMIO/PIO accesses
to discover queues and kick the device, the guest driver exchanges small
messages with QEMU. The virtio-msg-amp-pci device provides a PCI wrapper for
an AMP-style shared-memory transport. Each FIFO is a point-to-point message
channel backed by RAM and a doorbell register for notifications.

QEMU exposes a virtio-msg bus per FIFO. Devices are attached under the
``virtio-msg-amp-pci`` object and exchange virtio-msg protocol messages over
the FIFO. MSI-X interrupts are used to notify the guest when QEMU enqueues
messages for the driver.

Use case
--------

Virtio-msg is a virtio transport where driver and device communicate over
messages rather than using memory accesses that get trapped and emulated.
Virtio-msg depends on a lower level virtio-msg-bus responsible for delivering
these messages. In this case, we're using the virtio-msg AMP bus which moves
messages back and forth using a FIFO on top of shared-memory and interrupts.

The virtio-msg-amp-pci device exposes a BAR with RAM and doorbell registers
so guests can implement the shared-memory FIFO protocol and QEMU implements
the backend side of it.

Virtio-msg-amp-pci PCI device
-----------------------------

The virtio-msg-amp-pci device has the following layout:

- BAR 0: Registers (Version, features and notification/doorbell registers)
- BAR 1: RAM for FIFOs
- BAR 2: MSI-X table and PBA (created automatically)

Each FIFO gets an MSI-X interrupt reserved for it and a dedicated doorbell
register::

        REG32(VERSION,  0x00)
        REG32(FEATURES, 0x04)
        REG32(NOTIFY0,  0x20)
        REG32(NOTIFY1,  0x24)
        REG32(NOTIFY2,  0x28)
        And so on.

Each FIFO uses a 16 KiB window in BAR 1 with the following layout:

- 0 - 4 KiB: Reserved
- 4 - 8 KiB: Driver queue
- 8 - 12 KiB: Device queue
- 12 - 16 KiB: Reserved

The guest driver writes to the doorbell register to notify QEMU to process
the driver queue. QEMU posts responses on the device queue and raises the
FIFO's MSI-X vector to notify the guest.

How Does virtio-msg-amp-pci Compare to virtio-pci Emulation?
------------------------------------------------------------

Both virtio-msg-amp-pci and virtio-pci emulate PCI devices and allow users
to plug virtio devices behind them. The main difference is in how the
guest uses virtio-msg vs virtio-pci to discover and configure the virtio dev.

Virtio-msg-amp-pci Usage
------------------------

A virtio-msg-amp-pci can be created by adding the following to the QEMU
command-line::

    -device virtio-msg-amp-pci

Virtio devices can then be attached to the virtio-msg bus with for example
the following::

    -device virtio-rng-device,bus=/gpex-pcihost/pcie.0/virtio-msg-amp-pci/fifo0/virtio-msg/bus0/virtio-msg-dev

Multiple virtio devices can be connected by using bus1, bus2 and so on.

Device properties
-----------------

The virtio-msg-amp-pci device can be configured with the following properties:

 * ``num-fifos`` number of FIFOs (default 1, max 8).
