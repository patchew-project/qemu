.. _Sparc64-System-emulator:

Sparc64 System emulator
-----------------------

Use the executable ``qemu-system-sparc64`` to simulate a Sun4u
(UltraSPARC PC-like machine), Sun4v (T1 PC-like machine), or generic
Niagara (T1) machine. The Sun4u emulator is mostly complete, being able
to run Linux, NetBSD and OpenBSD in headless (-nographic) mode. The
Sun4v emulator is still a work in progress.

The Niagara T1 emulator makes use of firmware and OS binaries supplied
in the S10image/ directory of the OpenSPARC T1 project
http://download.oracle.com/technetwork/systems/opensparc/OpenSPARCT1_Arch.1.5.tar.bz2
and is able to boot the disk.s10hw2 Solaris image.

::

   qemu-system-sparc64 -M niagara -L /path-to/S10image/ \
                       -nographic -m 256 \
                       -drive if=pflash,readonly=on,file=/S10image/disk.s10hw2

QEMU emulates the following peripherals:

-  UltraSparc IIi APB PCI Bridge

-  PCI VGA compatible card with VESA Bochs Extensions

-  PS/2 mouse and keyboard

-  Non Volatile RAM M48T59

-  PC-compatible serial ports

-  2 PCI IDE interfaces with hard disk and CD-ROM support

-  Floppy disk

Every sun4[uv] machine reports a classic Sun "hostid" to the guest OS,
historically used by ``hostid(1)``, that some software use for license
checks. Normally it is built from a fixed machine id (defaults to 0x80)
byte plus the low 3 bytes of the machine's Ethernet MAC address. However,
these values can be overridden in the host's NVRAM. Since the current
emulated NVRAM isn't persistent, both halves can be overridden
independently using the ``hostid`` and ``machineid`` -machine
sub-properties::

    qemu-system-sparc64 -machine sun4u,hostid=0x352e09,machineid=0x72

``hostid=VALUE``
  Override the low 24 bits of the NVRAM/IDPROM hostid (accepts
  0x000000-0xffffff). Does not affect the emulated Lance NIC's MAC
  address, which is still set independently via ``-nic``/``-net``.

``machineid=VALUE``
  Override the machine-type byte (bits 31:24 of the hostid) normally
  fixed per machine model, e.g. 0x80 for sun4u (accepts 0x00-0xff).

If neither property is given, the behaviour is unchanged.
