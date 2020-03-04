Protected Virtualization on s390x
=================================

The memory and most of the register contents of Protected Virtual
Machines (PVMs) are inaccessible to the hypervisor, effectively
prohibiting VM introspection when the VM is running. At rest, PVMs are
encrypted and can only be decrypted by the firmware of specific IBM Z
machines.


Prerequisites
-------------

To run PVMs, you need to have a machine with the Protected
Virtualization feature, which is indicated by the Ultravisor Call
facility (stfle bit 158). This is a KVM only feature, therefore you
need a KVM which is able to support PVMs and activate the Ultravisor
initialization by setting `prot_virt=1` on the kernel command line.

If those requirements are met, the capability `KVM_CAP_S390_PROTECTED`
will indicate that KVM can support PVMs on that LPAR.


QEMU Settings
-------------

To indicate to the VM that it can move into protected mode, the
`Unpack facility` (stfle bit 161) needs to be part of the cpu model of
the VM.

All I/O devices need to use the IOMMU.
Passthrough (vfio) devices are currently not supported.

Host huge page backings are not supported. The guest however can use
huge pages as indicated by its facilities.


Boot Process
------------

A secure guest image can be both booted from disk and using the QEMU
command line. Booting from disk is done by the unmodified s390-ccw
BIOS. I.e., the bootmap is interpreted and a number of components is
read into memory and control is transferred to one of the components
(zipl stage3), which does some fixups and then transfers control to
some program residing in guest memory, which is normally the OS
kernel. The secure image has another component prepended (stage3a)
which uses the new diag308 subcodes 8 and 10 to trigger the transition
into secure mode.

Booting from the command line requires that the file passed
via -kernel has the same memory layout as would result from the disk
boot. This memory layout includes the encrypted components (kernel,
initrd, cmdline), the stage3a loader and metadata. In case this boot
method is used, the command line options -initrd and -cmdline are
ineffective.  The preparation of secure guest image is done by a
program (name tbd) of the s390-tools package.
