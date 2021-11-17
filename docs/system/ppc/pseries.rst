pSeries family boards (``pseries``)
===================================

The Power machine virtualized environment described by the `Linux on Power
Architecture Reference document (LoPAR)
<https://openpowerfoundation.org/wp-content/uploads/2020/07/LoPAR-20200611.pdf>`_
is called pseries. This environment is also known as sPAPR, System p guests, or
simply Power Linux guests (although it is capable of running other operating
systems, such as AIX).

Even though pseries is designed to behave as a guest environment, it is also
capable of acting as a hypervisor OS, providing, on that role, nested
virtualization capabilities.

Supported devices
-----------------

 * Multi processor support for many Power processors generations: POWER5+,
   POWER7, POWER7+, POWER8, POWER8NVL, Power9, and Power10 (there is no support
   for POWER6 processors).
 * Interrupt Controller, XICS (POWER8) and XIVE (Power9 and Power10)
 * vPHB PCIe Host bridge.
 * vscsi and vnet devices, compatible with the same devices available on a
   PowerVM hypervisor with VIOS managing LPARs.
 * Virtio based devices.
 * PCIe device pass through.

Missing devices
---------------

 * SPICE support.

Firmware
--------

`SLOF <https://github.com/aik/SLOF>`_ (Slimline Open Firmware) is an
implementation of the `IEEE 1275-1994, Standard for Boot (Initialization
Configuration) Firmware: Core Requirements and Practices
<https://standards.ieee.org/standard/1275-1994.html>`_.

QEMU includes a prebuilt image of SLOF which is updated when a more recent
version is required.

Build directions
----------------

.. code-block:: bash

  ./configure --target-list=ppc64-softmmu && make

Running instructions
--------------------

Someone can select the pseries machine type by running QEMU with the following
options:

.. code-block:: bash

  qemu-system-ppc64 -M pseries <other QEMU arguments>

sPAPR devices
-------------

The sPAPR specification defines a set of para-virtualized devices, which are
also supported by the pseries machine in QEMU and can be instantiated with the
`-device` option:

* spapr-vlan : A virtual network interface.
* spapr-vscsi : A virtual SCSI disk interface.
* spapr-rng : A pseudo-device for passing random number generator data to the
  guest (see the `H_RANDOM hypercall feature
  <https://wiki.qemu.org/Features/HRandomHypercall>`_ for details).

These are compatible with the devices historically available for use when
running the IBM PowerVM hypervisor with LPARs.

However, since these devices have originally been specified with another
hypervisor and non-Linux guests in mind, you should use the virtio counterparts
(virtio-net, virtio-blk/scsi and virtio-rng) if possible instead, since they
will most probably give you better performance with Linux guests in a QEMU
environment.

The pseries machine in QEMU is always instantiated with a NVRAM device
(``spapr-nvram``), so it is not needed to add this manually. However, if someone
wants to make the contents of the NVRAM device persistent, they will need to
specify a PFLASH device when starting QEMU, i.e. either use
``-drive if=pflash,file=<filename>,format=raw`` to set the default PFLASH
device, or specify one with an ID
(``-drive if=none,file=<filename>,format=raw,id=pfid``) and pass that ID to the
NVRAM device with ``-global spapr-nvram.drive=pfid``.

Switching between the KVM-PR and KVM-HV kernel module
-----------------------------------------------------

Currently, there are two implementations of KVM on Power, ``kvm_hv.ko`` and
``kvm_pr.ko``.


If a host supports both KVM modes, and both KVM kernel modules are loaded, it is
possible to switch between the two modes with the ``kvm-type`` parameter:

* Use ``qemu-system-ppc64 -M pseries,accel=kvm,kvm-type=PR`` to use the
  ``kvm_pr.ko`` kernel module.
* Use ``qemu-system-ppc64 -M pseries,accel=kvm,kvm-type=HV`` to use ``kvm_hv.ko``
  instead.

KVM-PR
^^^^^^

KVM-PR uses the so-called **PR**\ oblem state of the PPC CPUs to run the guests,
i.e. the virtual machine is run in user mode and all privileged instructions
trap and have to be emulated by the host. That means you can run KVM-PR inside
a pseries guest (or a PowerVM LPAR for that matter), and that is where it has
originated, as historically (prior to POWER7) it was not possible to run Linux
on hypervisor mode on a Power processor (this function was restricted to
PowerVM, the IBM proprietary hypervisor).

Because all privileged instructions are trapped, guests that use a lot of
privileged instructions run quite slow with KVM-PR. On the other hand, because
of that, this kernel module can run on pretty much every PPC hardware, and is
able to emulate a lot of guests CPUs. This module can even be used to run other
PowerPC guests like an emulated PowerMac.

As KVM-PR can be run inside a pseries guest, it can also provide nested
virtualization capabilities (i.e. running a guest from within a guest).

KVM-HV
^^^^^^

KVM-HV uses the hypervisor mode of more recent Power processors, that allow
access to the bare metal hardware directly. Although POWER7 had this capability,
it was only starting with POWER8 that this was officially supported by IBM.

Originally, KVM-HV was only available when running on a powernv platform (a.k.a.
Power bare metal). Although it runs on a powernv platform, it can only be used
to start pseries guests. As the pseries guest doesn't have access to the
hypervisor mode of the Power CPU, it wasn't possible to run KVM-HV on a guest.
This limitation has been lifted, and now it is possible to run KVM-HV inside
pseries guests as well, making nested virtualization possible with KVM-HV.

As KVM-HV has access to privileged instructions, guests that use a lot of these
can run much faster than with KVM-PR. On the other hand, the guest CPU has to be
of the same type as the host CPU this way, e.g. it is not possible to specify an
embedded PPC CPU for the guest with KVM-HV. However, there is at least the
possibility to run the guest in a backward-compatibility mode of the previous
CPUs generations, e.g. you can run a POWER7 guest on a POWER8 host by using
``-cpu POWER8,compat=power7`` as parameter to QEMU.

Modules support
---------------

As noticed in the sections above, each module can run in a different
environment. The following table shows with which environment each module can
run. As long as you are in a supported environment, you can run KVM-PR or KVM-HV
nested. Combinations not shown in the table are not available.

+--------------+------------+------+-------------------+----------+--------+
| Platform     | Host type  | Bits | Page table format | KVM-HV   | KVM-PR |
+==============+============+======+===================+==========+========+
| powernv      | bare metal | 32   | hash              | no       | yes    |
|              |            |      +-------------------+----------+--------+
|              |            |      | radix             | N/A      | N/A    |
|              |            +------+-------------------+----------+--------+
|              |            | 64   | hash              | yes      | yes    |
|              |            |      +-------------------+----------+--------+
|              |            |      | radix             | yes      | no     |
+--------------+------------+------+-------------------+----------+--------+
| pseries [*]_ | powernv    | 32   | hash              | no       | yes    |
|              |            |      +-------------------+----------+--------+
|              |            |      | radix             | N/A      | N/A    |
|              |            +------+-------------------+----------+--------+
|              |            | 64   | hash              | no       | yes    |
|              |            |      +-------------------+----------+--------+
|              |            |      | radix             | yes [*]_ | no     |
|              +------------+------+-------------------+----------+--------+
|              | PowerVM    | 32   | hash              | no       | yes    |
|              |            |      +-------------------+----------+--------+
|              |            |      | radix             | N/A      | N/A    |
|              |            +------+-------------------+----------+--------+
|              |            | 64   | hash              | no       | yes    |
|              |            |      +-------------------+----------+--------+
|              |            |      | radix [*]_        | no       | yes    |
+--------------+------------+------+-------------------+----------+--------+

.. [*] On POWER9 DD2.1 processors, the page table format on the host and guest
   must be the same.

.. [*] KVM-HV cannot run nested on POWER8 machines.

.. [*] Introduced on Power10 machines.

Maintainer contact information
------------------------------

Cédric Le Goater <clg@kaod.org>

Daniel Henrique Barboza <danielhb413@gmail.com>