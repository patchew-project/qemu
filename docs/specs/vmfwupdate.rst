VMFWUPDATE INTERFACE SPECIFICATION
##################################

Introduction
************

``Vmfwupdate`` is an extension to ``fw-cfg`` that allows guests to replace early boot
code in their virtual machine. Through a combination of vmfwupdate and
hypervisor stack knowledge, guests can deterministically replace the launch
payload for guests. This is useful for environments like SEV-SNP where the
launch payload becomes the launch digest. Guests can use vmfwupdate to provide
a measured, full guest payload (BIOS image, kernel, initramfs, kernel
command line) to the virtual machine which enables them to easily reason about
integrity of the resulting system.
For more information, please see the `KVM Forum 2024 presentation <KVMFORUM_>`__
about this work from the authors [1]_.


.. _KVMFORUM: https://www.youtube.com/watch?v=VCMBxU6tAto

Base Requirements
*****************

#. **fw-cfg**:
     The target system must provide a ``fw-cfg`` interface. For x86 based
     environments, this ``fw-cfg`` interface must be accessible through PIO ports
     0x510 and 0x511. The ``fw-cfg`` interface does not need to be announced as part
     of system device tables such as DSDT. The ``fw-cfg`` interface must support the
     DMA interface. It may only support the DMA interface for write operations.

#. **BIOS region**:
     The hypervisor must provide a BIOS region which may be
     statically sized. Through vmfwupdate, the guest is able to atomically replace
     its contents. The BIOS region must be mapped as read-write memory. In a
     SEV-SNP environment, the BIOS region must be mapped as private memory at
     launch time.

Fw-cfg Files
************

Guests drive vmfwupdate through special ``fw-cfg`` files that control its flow
followed by a standard system reset operation. When vmfwupdate is available,
it provides the following ``fw-cfg`` files:

* ``vmfwupdate/cap`` (``u64``) - Read-only Little Endian encoded bitmap of additional
  capabilities the interface supports. List of available capabilities:

     ``VMFWUPDATE_CAP_BIOS_RESIZE        0x0000000000000001``

* ``vmfwupdate/bios-size`` (``u32``) - Little Endian encoded size of the BIOS region.
  Read-only by default. Optionally Read-write if ``vmfwupdate/cap`` contains
  ``VMFWUPDATE_CAP_BIOS_RESIZE``. On write, the BIOS region may resize. Guests are
  required to read the value after writing and compare it with the requested size
  to determine whether the resize was successful. Note, x86 BIOS regions always
  start at 4GiB - bios-size.

* ``vmfwupdate/opaque`` (``1024 bytes``) - A 1KiB buffer that survives the BIOS replacement
  flow. Can be used by the guest to propagate guest physical addresses of payloads
  to its BIOS stage. It’s recommended to make the new BIOS clear this file on boot
  if it exists. Contents of this file are under control by the hypervisor. In an
  environment that considers the hypervisor outside of its trust boundary, guests
  are advised to validate its contents before consumption.

* ``vmfwupdate/disable`` (``u8``) - Indicates whether the interface is disabled.
  Returns 0 for enabled, 1 for disabled. Writing any value disables it. Writing is
  only allowed if the value is 0. When the interface is disabled, the replace file
  is ignored on reset. This value resets to 0 on system reset.

* ``vmfwupdate/bios-addr`` (``u64``) - A 64bit Little Endian encoded guest physical address
  at the beginning of the replacement BIOS region. The provided payload must reside
  in shared memory. 0 on system reset.


Triggering the Firmware Update
******************************

To initiate the firmware update process, the guest issues a standard system reset
operation through any of the means implemented by the machine model.

On reset, the hypervisor evaluates whether ``vmfwupdate/disable`` is ``1``. If it is, it ignores
any other vmfwupdate values and performs a standard system reset.

If ``vmfwupdate/disable`` is ``0``, the hypervisor checks if bios-addr is ``0``. If it is, it
performs a standard system reset.

If ``vmfwupdate/bios-addr`` is ``non-0``, the hypervisor replaces the contents of the system’s
BIOS region with the guest physically contiguous ``vmfwupdate/bios-size`` sized payload at the
guest physical address address vmfwupdate/bios-addr.

As part of the reset operation, all existing guest shared memory as well as the
``vmfwupdate/opaque`` file are preserved. CPU and device state are reset to the default
hypervisor specific reset states. In SEV-SNP environments, the reset causes recreation
of the VM context which triggers a fresh measurement of the replaced BIOS region and
reset CPU state. The guest always resumes operation in the highest privileged mode
available to it (VMPL0 in SEV-SNP).

Closing Remarks
***************
The handover protocol (format of the ``vmwupdate/opaque`` file etc.) will be implemented by
the firmware loader and firmware image, both provided by the guest.  The hypervisor does
not need to know these details, so it is not included in this specification.



Footnotes:
^^^^^^^^^^
.. [1] Original author of the specification: *Alex Graf <graf@amazon.com>*,
       converted to re-structured-text (rst format) and slightly edited
       by *Ani Sinha <anisinha@redhat.com>*.
