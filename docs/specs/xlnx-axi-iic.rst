.. SPDX-License-Identifier: GPL-2.0-or-later

Xilinx AXI IIC device
======================

``xlnx-axi-iic`` models the AMD/Xilinx AXI IIC (LogiCORE IP, documented in
Xilinx PG090) I2C bus controller. It is a SysBus device: a board or a parent
device maps its single MMIO region and connects its interrupt line, and I2C
slave models are attached to the ``i2c`` bus it creates.

The model implements the controller's *dynamic* transfer mode, which is what
a guest driver uses by default.

Properties
----------

``bus-name``
  Name given to the I2C bus the controller creates (default ``i2c``). A parent
  device that instantiates several controllers uses this to give each bus a
  unique, user-referenceable name.

MMIO register map
-----------------

Offsets are relative to the start of the controller's MMIO region.

  0x1C (RW) : DGIER, global interrupt enable (bit 31)
  0x20 (RW) : IISR, interrupt status; write-1-to-clear
  0x28 (RW) : IIER, interrupt enable
  0x40 (WO) : RESETR, soft reset (write 0xA)
  0x100 (RW) : CR, control
  0x104 (RO) : SR, status (computed)
  0x108 (WO) : DTR, tx data and dynamic START (bit 8) / STOP (bit 9)
  0x10C (RO) : DRR, rx data
  0x114 (RO) : TFO, tx FIFO occupancy (always 0; the FIFO drains immediately)
  0x118 (RO) : RFO, rx FIFO occupancy
  0x120 (RW) : RFD, rx FIFO programmable depth

Status register (SR) bits: 0x04 bus busy, 0x20 rx FIFO full, 0x40 rx FIFO
empty, 0x80 tx FIFO empty. Interrupt (IISR/IIER) bits: 0x01 arbitration lost,
0x02 tx error / NACK, 0x04 tx FIFO empty, 0x08 rx FIFO full, 0x10 bus-not-busy.

Dynamic-mode transfers
----------------------

The low 8 bits of a DTR write are the data byte; bit 8 (START) frames the
8-bit address (bit 0 is the read/write flag) that opens a transfer, and bit 9
(STOP) ends it.

- Write: DTR <- addr|START, then each data byte, the last with STOP.
- Read: DTR <- addr|START (read flag set), then DTR <- count|STOP. The
  controller clocks ``count`` bytes from the slave into the rx FIFO, raises
  IISR.RX_FULL, and raises IISR.BNB once the FIFO has been drained through DRR.

A slave that does not acknowledge sets IISR.TX_ERROR and releases the bus.

Interrupt
---------

The controller drives a single level-triggered output line, asserted while
DGIER is enabled and ``IISR & IIER`` is non-zero, and deasserted when the guest
clears the pending, enabled causes.
