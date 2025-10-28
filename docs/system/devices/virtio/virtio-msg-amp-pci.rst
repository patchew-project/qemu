Virtio-msg AMP PCI
==================

This document explains the setup and usage of the virtio-msg-amp-pci device..
The virtio-msg-amp-pci, is an emulated PCI device that provides a small
set of features to enable virtio-msg over shared-memory queue's.

Usecase
-------

Virtio-msg is a virtio transport where driver and device communicate over
messages rather than using memory accesses that get trapped and emulated.
Virtio-msg depends on a lower lever virtio-msg-bus responsible for delivering
these messages. In this case, we're using the Virtio-msg AMP bus which moves
messages back and forth using a FIFO on top of shared-memory and interrupts.

The virtio-msg-amp-pci device exposes a BAR with RAM and doorbell registers
so guests can implement the shared-memory FIFO protocol and QEMU implements
the backend side of it.

Virtio pmem allows to bypass the guest page cache and directly use
host page cache. This reduces guest memory footprint as the host can
make efficient memory reclaim decisions under memory pressure.

Virtio-msg-amp-pci PCI device
-----------------------------

The virtio-msg-amp-pci device has the following layout:

- BAR 0: Registers (Version, features and notification/doorbell regs)
- BAR 1: RAM for FIFOs

Each FIFO gets an MSI-X interrupt reserved for it and a dedicated doorbell
register::

        REG32(VERSION,  0x00)
        REG32(FEATURES, 0x04)
        REG32(NOTIFY0,  0x20)
        REG32(NOTIFY1,  0x24)
        REG32(NOTIFY2,  0x28)
        And so on.

How does virtio-msg-amp-pci compare to virtio-pci emulation?
------------------------------------------------------------

Both virtio-msg-amp-pci and virtio-pci emulate PCI devices and allow users
to plug virtio devices behind them. The main difference is in how the
guest uses virtio-msg vs virtio-pci to discover and configure the virtio dev.

virtio pmem usage
-----------------

A virtio-msg-amp-pci can be greated by adding the following to the QEMU
command-line::

    -device virtio-msg-amp-pci

Virtio devices can then be attached to the virtio-msg bus with for example
the following::

    -device virtio-rng-device,bus=/gpex-pcihost/pcie.0/virtio-msg-amp-pci/fifo0/virtio-msg/bus0/virtio-msg-dev

Multiple virtio devices can be connected by using bus1, bus2 and so on.

Device properties
-----------------

The virtio-msg-amp-pci  device can be configured with the following properties:

 * ``num-fifos`` number of fifos (default 2).
