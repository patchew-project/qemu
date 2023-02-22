CPU topology on s390x
=====================

Since QEMU 8.0, CPU topology on s390x provides up to 3 levels of
topology containers: drawers, books, sockets, defining a tree shaped
hierarchy.

The socket container contains one or more CPU entries consisting
of a bitmap of three dentical CPU attributes:

- CPU type
- polarization entitlement
- dedication

Note also that since 7.2 threads are no longer supported in the topology
and the ``-smp`` command line argument accepts only ``threads=1``.

Prerequisites
-------------

To use CPU topology a Linux QEMU/KVM machine providing the CPU topology facility
(STFLE bit 11) is required.

However, since this facility has been enabled by default in an early version
of QEMU, we use a capability, ``KVM_CAP_S390_CPU_TOPOLOGY``, to notify KVM
that QEMU supports CPU topology.

Enabling CPU topology
---------------------

Currently, CPU topology is only enabled in the host model by default.

Enabling CPU topology in a CPU model is done by setting the CPU flag
``ctop`` to ``on`` like in:

.. code-block:: bash

   -cpu gen16b,ctop=on

Having the topology disabled by default allows migration between
old and new QEMU without adding new flags.

Default topology usage
----------------------

The CPU topology can be specified on the QEMU command line
with the ``-smp`` or the ``-device`` QEMU command arguments.

If none of the containers attributes (drawers, books, sockets) are
specified for the ``-smp`` flag, the number of these containers
is ``1`` .

.. code-block:: bash

    -smp cpus=5,drawer=1,books=1,sockets=8,cores=4,maxcpus=32

or

.. code-block:: bash

    -smp cpus=5,sockets=8,cores=4,maxcpus=32

When a CPU is defined by the ``-smp`` command argument, its position
inside the topology is calculated by adding the CPUs to the topology
based on the core-id starting with core-0 at position 0 of socket-0,
book-0, drawer-0 and filling all CPUs of socket-0 before to fill socket-1
of book-0 and so on up to the last socket of the last book of the last
drawer.

When a CPU is defined by the ``-device`` command argument, the
tree topology attributes must be all defined or all not defined.

.. code-block:: bash

    -device gen16b-s390x-cpu,drawer-id=1,book-id=1,socket-id=2,core-id=1

or

.. code-block:: bash

    -device gen16b-s390x-cpu,core-id=1,dedication=true

If none of the tree attributes (drawer, book, sockets), are specified
for the ``-device`` argument, as for all CPUs defined on the ``-smp``
command argument the topology tree attributes will be set by simply
adding the CPUs to the topology based on the core-id starting with
core-0 at position 0 of socket-0, book-0, drawer-0.

QEMU will not try to solve collisions and will report an error if the
CPU topology, explicitely or implicitely defined on a ``-device``
argument collides with the definition of a CPU implicitely defined
on the ``-smp`` argument.

When the topology modifier attributes are not defined for the
``-device`` command argument they takes following default values:

- dedication: ``false``
- entitlement: ``medium``


Hot plug
++++++++

New CPUs can be plugged using the device_add hmp command as in:

.. code-block:: bash

  (qemu) device_add gen16b-s390x-cpu,core-id=9

The same placement of the CPU is derived from the core-id as described above.

The topology can of course be fully defined:

.. code-block:: bash

    (qemu) device_add gen16b-s390x-cpu,drawer-id=1,book-id=1,socket-id=2,core-id=1


Examples
++++++++

In the following machine we define 8 sockets with 4 cores each.

.. code-block:: bash

  $ qemu-system-s390x -m 2G \
    -cpu gen16b,ctop=on \
    -smp cpus=5,sockets=8,cores=4,maxcpus=32 \
    -device host-s390x-cpu,core-id=14 \

A new CPUs can be plugged using the device_add hmp command as before:

.. code-block:: bash

  (qemu) device_add gen16b-s390x-cpu,core-id=9

The core-id defines the placement of the core in the topology by
starting with core 0 in socket 0 up to maxcpus.

In the example above:

* There are 5 CPUs provided to the guest with the ``-smp`` command line
  They will take the core-ids 0,1,2,3,4
  As we have 4 cores in a socket, we have 4 CPUs provided
  to the guest in socket 0, with core-ids 0,1,2,3.
  The last cpu, with core-id 4, will be on socket 1.

* the core with ID 14 provided by the ``-device`` command line will
  be placed in socket 3, with core-id 14

* the core with ID 9 provided by the ``device_add`` qmp command will
  be placed in socket 2, with core-id 9


Polarization, entitlement and dedication
----------------------------------------

Polarization
++++++++++++

The polarization is an indication given by the ``guest`` to the host
that it is able to make use of CPU provisioning information.
The guest indicates the polarization by using the PTF instruction.

Polarization is define two models of CPU provisioning: horizontal
and vertical.

The horizontal polarization is the default model on boot and after
subsystem reset in which the guest considers all vCPUs being having
an equal provisioning of CPUs by the host.

In the vertical polarization model the guest can make use of the
vCPU entitlement information provided by the host to optimize
kernel thread scheduling.

A subsystem reset puts all vCPU of the configuration into the
horizontal polarization.

Entitlement
+++++++++++

The vertical polarization specifies that guest's vCPU can get
different real CPU provisions:

- a vCPU with vertical high entitlement specifies that this
  vCPU gets 100% of the real CPU provisioning.

- a vCPU with vertical medium entitlement specifies that this
  vCPU shares the real CPU with other vCPUs.

- a vCPU with vertical low entitlement specifies that this
  vCPU only gets real CPU provisioning when no other vCPUs needs it.

In the case a vCPU with vertical high entitlement does not use
the real CPU, the unused "slack" can be dispatched to other vCPU
with medium or low entitlement.

The admin specifies a vCPU as ``dedicated`` when the vCPU is fully dedicated
to a single real CPU.

The dedicated bit is an indication of affinity of a vCPU for a real CPU
while the entitlement indicates the sharing or exclusivity of use.

Defining the topology on command line
-------------------------------------

The topology can entirely be defined using -device cpu statements,
with the exception of CPU 0 which must be defined with the -smp
argument.

For example, here we set the position of the cores 1,2,3 to
drawer 1, book 1, socket 2 and cores 0,9 and 14 to drawer 0,
book 0, socket 0 with all horizontal polarization and not dedicated.
The core 4, will be set on its default position on socket 1
(since we have 4 core per socket) and we define it with dedication and
vertical high entitlement.

.. code-block:: bash

  $ qemu-system-s390x -m 2G \
    -cpu gen16b,ctop=on \
    -smp cpus=1,sockets=8,cores=4,maxcpus=32 \
    \
    -device gen16b-s390x-cpu,drawer-id=1,book-id=1,socket-id=2,core-id=1 \
    -device gen16b-s390x-cpu,drawer-id=1,book-id=1,socket-id=2,core-id=2 \
    -device gen16b-s390x-cpu,drawer-id=1,book-id=1,socket-id=2,core-id=3 \
    \
    -device gen16b-s390x-cpu,drawer-id=0,book-id=0,socket-id=0,core-id=9 \
    -device gen16b-s390x-cpu,drawer-id=0,book-id=0,socket-id=0,core-id=14 \
    \
    -device gen16b-s390x-cpu,core-id=4,dedicated=on,polarization=3 \

QAPI interface for topology
---------------------------

Let's start QEMU with the following command:

.. code-block:: bash

 qemu-system-s390x \
    -enable-kvm \
    -cpu z14,ctop=on \
    -smp 1,drawers=3,books=3,sockets=2,cores=2,maxcpus=36 \
    \
    -device z14-s390x-cpu,core-id=19,polarization=3 \
    -device z14-s390x-cpu,core-id=11,polarization=1 \
    -device z14-s390x-cpu,core-id=112,polarization=3 \
   ...

and see the result when using the QAPI interface.

addons to query-cpus-fast
+++++++++++++++++++++++++

The command query-cpus-fast allows the admin to query the topology
tree and modifiers for all configured vCPUs.

.. code-block:: QMP

 { "execute": "query-cpus-fast" }
 {
  "return": [
    {
      "dedicated": false,
      "thread-id": 536993,
      "props": {
        "core-id": 0,
        "socket-id": 0,
        "drawer-id": 0,
        "book-id": 0
      },
      "cpu-state": "operating",
      "entitlement": "medium",
      "qom-path": "/machine/unattached/device[0]",
      "cpu-index": 0,
      "target": "s390x"
    },
    {
      "dedicated": false,
      "thread-id": 537003,
      "props": {
        "core-id": 19,
        "socket-id": 1,
        "drawer-id": 0,
        "book-id": 2
      },
      "cpu-state": "operating",
      "entitlement": "high",
      "qom-path": "/machine/peripheral-anon/device[0]",
      "cpu-index": 19,
      "target": "s390x"
    },
    {
      "dedicated": false,
      "thread-id": 537004,
      "props": {
        "core-id": 11,
        "socket-id": 1,
        "drawer-id": 0,
        "book-id": 1
      },
      "cpu-state": "operating",
      "entitlement": "low",
      "qom-path": "/machine/peripheral-anon/device[1]",
      "cpu-index": 11,
      "target": "s390x"
    },
    {
      "dedicated": true,
      "thread-id": 537005,
      "props": {
        "core-id": 112,
        "socket-id": 0,
        "drawer-id": 3,
        "book-id": 2
      },
      "cpu-state": "operating",
      "entitlement": "high",
      "qom-path": "/machine/peripheral-anon/device[2]",
      "cpu-index": 112,
      "target": "s390x"
    }
  ]
 }


set-cpu-topology
++++++++++++++++

The command set-cpu-topology allows the admin to modify the topology
tree or the topology modifiers of a vCPU in the configuration.

.. code-block:: QMP

 -> { "execute": "set-cpu-topology",
      "arguments": {
         "core-id": 11,
         "socket-id": 0,
         "book-id": 0,
         "drawer-id": 0,
         "entitlement": low,
         "dedicated": false
      }
    }
 <- {"return": {}}

The core-id parameter is the only non optional parameter and every
unspecified parameter keeps its previous value.

event CPU_POLARIZATION_CHANGE
+++++++++++++++++++++++++++++

When a guest is requests a modification of the polarization,
QEMU sends a CPU_POLARIZATION_CHANGE event.

When requesting the change, the guest only specifies horizontal or
vertical polarization.
It is the job of the admin to set the dedication and fine grained vertical entitlement
in response to this event.

Note that a vertical polarized dedicated vCPU can only have a high
entitlement, this gives 6 possibilities for vCPU polarization:

- Horizontal
- Horizontal dedicated
- Vertical low
- Vertical medium
- Vertical high
- Vertical high dedicated

Example of the event received when the guest issues the CPU instruction
Perform Topology Function PTF(0) to request an horizontal polarization:

.. code-block:: QMP

 <- { "event": "CPU_POLARIZATION_CHANGE",
      "data": { "polarization": 0 },
      "timestamp": { "seconds": 1401385907, "microseconds": 422329 } }
