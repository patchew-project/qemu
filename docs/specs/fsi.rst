======================================
IBM's Flexible Service Interface (FSI)
======================================

The QEMU FSI emulation implements hardware interfaces between ASPEED SOC, FSI
master/slave and the end engine.

FSI is a point-to-point two wire interface which is capable of supporting
distances of up to 4 meters. FSI interfaces have been used successfully for
many years in IBM servers to attach IBM Flexible Support Processors(FSP) to
CPUs and IBM ASICs.

FSI allows a service processor access to the internal buses of a host POWER
processor to perform configuration or debugging. FSI has long existed in POWER
processes and so comes with some baggage, including how it has been integrated
into the ASPEED SoC.

Working backwards from the POWER processor, the fundamental pieces of interest
for the implementation are: (see the `FSI specification`_ for more details)

1. The Common FRU Access Macro (CFAM), an address space containing various
   "engines" that drive accesses on buses internal and external to the POWER
   chip. Examples include the SBEFIFO and I2C masters. The engines hang off of
   an internal Local Bus (LBUS) which is described by the CFAM configuration
   block.

2. The FSI slave: The slave is the terminal point of the FSI bus for FSI
   symbols addressed to it. Slaves can be cascaded off of one another. The
   slave's configuration registers appear in address space of the CFAM to
   which it is attached.

3. The FSI master: A controller in the platform service processor (e.g. BMC)
   driving CFAM engine accesses into the POWER chip. At the hardware level
   FSI is a bit-based protocol supporting synchronous and DMA-driven accesses
   of engines in a CFAM.

4. The On-Chip Peripheral Bus (OPB): A low-speed bus typically found in POWER
   processors. This now makes an appearance in the ASPEED SoC due to tight
   integration of the FSI master IP with the OPB, mainly the existence of an
   MMIO-mapping of the CFAM address straight onto a sub-region of the OPB
   address space.

5. An APB-to-OPB bridge enabling access to the OPB from the Arm core in the
   AST2600. Hardware limitations prevent the OPB from being directly mapped
   into APB, so all accesses are indirect through the bridge.

The LBUS is modelled to maintain the qdev bus hierarchy and to take advantages
of the object model to automatically generate the CFAM configuration block.
The configuration block presents engines in the order they are attached to the
CFAM's LBUS. Engine implementations should subclass the LBusDevice and set the
'config' member of LBusDeviceClass to match the engine's type.

CFAM designs offer a lot of flexibility, for instance it is possible for a
CFAM to be simultaneously driven from multiple FSI links. The modeling is not
so complete; it's assumed that each CFAM is attached to a single FSI slave (as
a consequence the CFAM subclasses the FSI slave).

As for FSI, its symbols and wire-protocol are not modelled at all. This is not
necessary to get FSI off the ground thanks to the mapping of the CFAM address
space onto the OPB address space - the models follow this directly and map the
CFAM memory region into the OPB's memory region.

The following commands start the ``rainier-bmc`` machine with built-in FSI
model. There are no model specific arguments. Please check this document to
learn more about Aspeed ``rainier-bmc`` machine: (:doc:`../../system/arm/aspeed`)

.. code-block:: console

  qemu-system-arm -M rainier-bmc -nographic \
  -kernel fitImage-linux.bin \
  -dtb aspeed-bmc-ibm-rainier.dtb \
  -initrd obmc-phosphor-initramfs.rootfs.cpio.xz \
  -drive file=obmc-phosphor-image.rootfs.wic.qcow2,if=sd,index=2 \
  -append "rootwait console=ttyS4,115200n8 root=PARTLABEL=rofs-a"

The implementation appears as following in the qemu device tree:

.. code-block:: console

  (qemu) info qtree
  bus: main-system-bus
    type System
    ...
    dev: aspeed.apb2opb, id ""
      gpio-out "sysbus-irq" 1
      mmio 000000001e79b000/0000000000001000
      bus: opb.1
        type opb
        dev: fsi.master, id ""
          bus: fsi.bus.1
            type fsi.bus
            dev: cfam.config, id ""
            dev: cfam, id ""
              bus: lbus.1
                type lbus
                dev: scratchpad, id ""
                  address = 0 (0x0)
      bus: opb.0
        type opb
        dev: fsi.master, id ""
          bus: fsi.bus.0
            type fsi.bus
            dev: cfam.config, id ""
            dev: cfam, id ""
              bus: lbus.0
                type lbus
                dev: scratchpad, id ""
                  address = 0 (0x0)

pdbg is a simple application to allow debugging of the host POWER processors
from the BMC. (see the `pdbg source repository`_ for more details)

.. code-block:: console

  root@p10bmc:~# pdbg -a getcfam 0x0
  p0: 0x0 = 0xc0022d15

.. _FSI specification:
   https://openpowerfoundation.org/specifications/fsi/

.. _pdbg source repository:
   https://github.com/open-power/pdbg

CFAM-S model (AST2700)
----------------------

The AST2700 uses a CFAM variant known as the CFAM-S, required by the new
Linux FSI responder framework. A dedicated QOM type, ``TYPE_FSI_CFAM_S``
(``"cfam-s"``), implements this alongside the existing ``TYPE_FSI_CFAM``
(``"cfam"``) used by the AST2600 machines.

``FSIMasterState`` realizes both ``cfam`` and ``cfam-s``. The regular
``cfam`` is mapped at offset 0 into the OPB-to-FSI aperture; ``cfam-s`` is
mapped at offset 2 MiB. The DTS overrides ``fsim0`` with
``compatible = "aspeed,ast2600-fsi-master"`` and ``reg = <0x21800000>``,
pointing the kernel FSI driver at the QEMU APB-to-OPB bridge, which routes
through the FSI master's OPB-to-FSI window to reach the CFAM-S.

The CFAM-S model presents an 8 MiB region and folds the SID bits ``[22:21]``
so both the SID_BREAK enumeration view (``0x600000``) and the runtime view
(``0x000000``) address the same register space. The config table at folded
offset ``0x000`` contains three CRC4-valid words: a chip_id word with
``MAJOR=9`` (selecting the ``cfam_s`` kernel driver), a responder engine
entry (``TYPE=0x3``), and a mailbox v1 engine entry (``TYPE=0x14``) at
engine address ``0x800``.

The responder registers at folded offset ``0x400`` implement ``SMODE``,
``SSTAT``, ``SRES``, ``SSISM``, and ``SLBUS`` with store-on-write semantics.
The mailbox scratch registers at folded offset ``0x8e0`` are five stateful
32-bit registers served to the ``ibm,mbox-cfam-s`` kernel driver, which
creates ``/dev/fsi/mbox0``.

The following commands start the ``huygens-bmc`` machine with the built-in
CFAM-S model. There are no model specific arguments. Please check this
document to learn more about Aspeed ``huygens-bmc`` machine:
(:doc:`../../system/arm/aspeed`)

.. code-block:: console

  qemu-system-aarch64 -M huygens-bmc \
    -drive file=image-bmc,if=mtd,format=raw \
    -drive file=ufs.img,if=none,format=raw \
    -nographic

The CFAM-S appears as follows in the QEMU device tree:

.. code-block:: console

  (qemu) info qtree
  bus: main-system-bus
    type System
    ...
    dev: aspeed.apb2opb, id ""
      mmio 0000000021800000/0000000000001000
      bus: opb.0
        type opb
        dev: fsi.master, id ""
          bus: fsi.bus.0
            type fsi.bus
            dev: cfam-s, id ""
