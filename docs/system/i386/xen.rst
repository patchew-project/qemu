Xen HVM guest support
=====================


Description
-----------

KVM has support for hosting Xen guests, intercepting Xen hypercalls and event
channel (Xen PV interrupt) delivery. This allows guests which expect to be
run under Xen to be hosted in QEMU under Linux/KVM instead.

Setup
-----

Xen mode is enabled by setting the ``xen-version`` property of the KVM
accelerator to a 32-bit value in the ``XENVER_version`` form, with the Xen
major version in the top 16 bits and the minor version in the low 16 bits,
for example for Xen 4.10:

.. parsed-literal::

  |qemu_system| --accel kvm,xen-version=0x4000a

Additionally, virtual APIC support can be advertised to the guest through the
``xen-vapic`` CPU flag:

.. parsed-literal::

  |qemu_system| --accel kvm,xen-version=0x4000a --cpu host,+xen_vapic

When Xen support is enabled, QEMU changes hypervisor identification (CPUID
0x40000000..0x4000000A) to Xen. The KVM identification and features are not
advertised to a Xen guest. If Hyper-V is also enabled, the Xen identification
moves to leaves 0x40000100..0x4000010A.

The Xen platform device is enabled automatically for a Xen guest. This allows
a guest to unplug all emulated devices, in order to use Xen PV block and network
drivers instead. Note that until the Xen PV device back ends are enabled to work
with Xen mode in QEMU, that is unlikely to cause significant joy. Linux guests
can be dissuaded from this by adding 'xen_emul_unplug=never' on their command
line, and it can also be noted that AHCI disk controllers are exempt from being
unplugged, as are passthrough VFIO PCI devices.

OS requirements
---------------

The minimal Xen support in the KVM accelerator requires the host to be running
Linux v5.12 or newer. Later versions add optimisations: Linux v5.17 added
acceleration of interrupt delivery via the Xen PIRQ mechanism, and Linux v5.19
accelerated Xen PV timers and inter-processor interrupts (IPIs).
