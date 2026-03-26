Windows Hypervisor Platform
===========================

Common
------

Windows Hypervisor Platform is available for installation through
Windows Features (`optionalfeatures.exe`).

VM state save/restore is not implemented.

Known issues on x86_64
----------------------

Guests using legacy VGA modes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In guests using VGA modes that QEMU doesn't pass through framebuffer
memory for, performance will be quite suboptimal.

Workaround: for affected guests, use a more modern graphics mode.
Alternatively, use TCG to run those guests.

`-M isapc`
^^^^^^^^^^

`-M isapc` doesn't disable the Hyper-V LAPIC on its own yet. To
be able to use that machine, use `-accel whpx,hyperv=off,kernel-irqchip=off`.

However, in QEMU 11.0, the guest will still be a 64-bit x86
ISA machine with all the corresponding CPUID leaves exposed.

gdbstub
^^^^^^^

As save/restore of xsave state is not currently present, state
exposed through GDB will be incomplete.

The same also applies to `info registers`.

-cpu `type` ignored
^^^^^^^^^^^^^^^^^^^

In this release, -cpu is an ignored argument. 

PIC interrupts on Windows 10
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

QEMU's Windows Hypervisor Platform backend is tested starting from
Windows 10 version 2004. Earlier Windows 10 releases *might* work
but are not tested.

On Windows 10, a legacy PIC interrupt injected does not wake the guest
from an HLT when using the Hyper-V provided interrupt controller.

This has been addressed in QEMU 11.0 on Windows 11 platforms but
functionality to make it available on Windows 10 isn't present.

Workaround: for affected use cases, use -M kernel-irqchip=off.

Known issues on Windows 11
^^^^^^^^^^^^^^^^^^^^^^^^^^

Nested virtualisation-specific Hyper-V enlightenments are not
currently exposed.

arm64
-----

OS baseline
^^^^^^^^^^^

On Windows 11, Windows 11 24H2 with the April 2025 optional updates
or May 2025 security updates is the minimum required release. 

Prior releases of Windows 11 version 24H2 shipped with a pre-release
version of the Windows Hypervisor Platform API, which is not 
supported in QEMU.

ISA feature support
^^^^^^^^^^^^^^^^^^^

SVE and SME are not currently supported.
