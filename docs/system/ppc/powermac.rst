PowerMac family boards (``g3beige``, ``mac99``)
==================================================================

Use the executable ``qemu-system-ppc`` to simulate a complete PowerMac
PowerPC system.

- ``g3beige``           Heathrow based old world Power Macintosh G3
- ``mac99``             Core99 based generic PowerMac
- ``powermac3_1``       Power Mac G4 AGP (Sawtooth)
- ``powerbook3_2``      PowerBook G4 Titanium (Mercury)
- ``powermac7_3``       Power Mac G5 (Niagara) (only in ``qemu-system-ppc64``)


Supported devices
-----------------

QEMU emulates the following PowerMac peripherals:

 *  UniNorth or Grackle PCI Bridge
 *  PCI VGA compatible card with VESA Bochs Extensions
 *  2 PMAC IDE interfaces with hard disk and CD-ROM support
 *  Sungem PCI network adapter
 *  Non Volatile RAM
 *  VIA-CUDA or VIA-PMU99 with or without ADB or USB keyboard and mouse.


Missing devices
---------------

 * To be identified

Firmware
--------

Since version 0.9.1, QEMU uses OpenBIOS https://www.openbios.org/ for
the g3beige and mac99 PowerMac and the 40p machines. OpenBIOS is a free
(GPL v2) portable firmware implementation. The goal is to implement a
100% IEEE 1275-1994 (referred to as Open Firmware) compliant firmware.
