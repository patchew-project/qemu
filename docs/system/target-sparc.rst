.. _Sparc32-System-emulator:

Sparc32 System emulator
-----------------------

Use the executable ``qemu-system-sparc`` to simulate the following Sun4m
architecture machines:

-  SPARCstation 4

-  SPARCstation 5

-  SPARCstation 10

-  SPARCstation 20

-  SPARCserver 600MP

-  SPARCstation LX

-  SPARCstation Voyager

-  SPARCclassic

-  SPARCbook

The emulation is somewhat complete. SMP up to 16 CPUs is supported, but
Linux limits the number of usable CPUs to 4.

The list of available CPUs can be viewed by starting QEMU with ``-cpu help``.
Optional boolean features can be added with a "+" in front of the feature name,
or disabled with a "-" in front of the name, for example
``-cpu TI-SuperSparc-II,+float128``.

QEMU emulates the following sun4m peripherals:

-  IOMMU

-  TCX or cgthree Frame buffer

-  Lance (Am7990) Ethernet

-  Non Volatile RAM M48T02/M48T08

-  Slave I/O: timers, interrupt controllers, Zilog serial ports,
   :ref:`keyboard` and power/reset logic

-  ESP SCSI controller with hard disk and CD-ROM support

-  Floppy drive (not on SS-600MP)

-  CS4231 sound device (only on SS-5, not working yet)

The number of peripherals is fixed in the architecture. Maximum memory
size depends on the machine type, for SS-5 it is 256MB and for others
2047MB.

Every sun4[cdm] machine reports a classic Sun "hostid" to the guest OS,
historically used by ``hostid(1)``, that some software use for license
checks. Normally it is built from a fixed per-model machine-type byte
plus the low 3 bytes of the machine's Ethernet MAC address. However,
these values can be overridden in the host's NVRAM. Since the current
emulated NVRAM isn't persistent, both halves can be overridden
independently using the ``hostid`` and ``machineid`` -machine
sub-properties::

    qemu-system-sparc -machine SS-20,hostid=0x352e09,machineid=0x80

``hostid=VALUE``
  Override the low 24 bits of the NVRAM/IDPROM hostid (accepts
  0x000000-0xffffff). Does not affect the emulated Lance NIC's MAC
  address, which is still set independently via ``-nic``/``-net``.

``machineid=VALUE``
  Override the machine-type byte (bits 31:24 of the hostid) normally
  fixed per machine model, e.g. 0x80 for SS-5, 0x72 for SS-20 (accepts
  0x00-0xff).

If neither property is given, the behaviour is unchanged.

Since version 0.8.2, QEMU uses OpenBIOS https://www.openbios.org/.
OpenBIOS is a free (GPL v2) portable firmware implementation. The goal
is to implement a 100% IEEE 1275-1994 (referred to as Open Firmware)
compliant firmware.

Please note that currently older Solaris kernels don't work; this is probably
due to interface issues between OpenBIOS and Solaris.
