==============
UEFI variables
==============

Guest UEFI variable management
==============================

Traditional approach for UEFI Variable storage in qemu guests is to
work as close as possible to physical hardware.  That means provide
pflash as storage and leave the management of variables and flash to
the guest.

Secure boot support comes with the requirement that the UEFI variable
storage must be protected against direct access by the OS.  All update
requests must pass the sanity checks.  (Parts of) the firmware must
run with a higher priviledge level than the OS so this can be enforced
by the firmware.  On x86 this has been implemented using System
Management Mode (SMM) in qemu and kvm, which again is the same
approach taken by physical hardware.  Only priviedged code running in
SMM mode is allowed to access flash storage.

Communication with the firmware code running in SMM mode works by
serializing the requests to a shared buffer, then trapping into SMM
mode via SMI.  The SMM code processes the request, stores the reply in
the same buffer and returns.

Host UEFI variable service
==========================

Instead of running the priviledged code inside the guest we can run it
on the host.  The serialization protocol cen be reused.  The
communication with the host uses a virtual device, which essentially
allows to configure the shared buffer location and size and to trap to
the host to process the requests.

The ``uefi-vars`` device implements the UEFI virtual device.  It comes
in ``uefi-vars-isa`` and ``uefi-vars-sysbus`` flavours.  The device
reimplements the handlers needed, specifically
``EfiSmmVariableProtocol`` and ``VarCheckPolicyLibMmiHandler``.  It
also consumes events (``EfiEndOfDxeEventGroup``,
``EfiEventReadyToBoot`` and ``EfiEventExitBootServices``).

The advantage of the approach is that we do not need a special
prividge level for the firmware to protect itself, i.e. it does not
depend on SMM emulation on x64, which allows to remove a bunch of
complex code for SMM emulation from the linux kernel
(CONFIG_KVM_SMM=n).  It also allows to support secure boot on arm
without implementing secure world (el3) emulation in kvm.

Of course there are also downsides.  The added device increases the
attack surface of the host, and we are adding some code duplication
because we have to reimplement some edk2 functionality in qemu.

usage on x86_64 (isa)
---------------------

.. code::

   qemu-system-x86_64 -device uefi-vars-isa,jsonfile=/path/to/vars.json

usage on aarch64 (sysbus)
-------------------------

.. code::

   qemu-system-aarch64 -M virt,x-uefi-vars=on
