CPU topology on s390x
=====================

Since QEMU 8.0, CPU topology on s390x provides up to 4 levels of
topology containers: drawers, books, sockets and cores.

The first three containers define a tree hierarchy, the last one
provides the placement of the CPUs inside the parent container and
3 CPU attributes:

- CPU type
- polarity entitlement
- dedication

Note also that since 7.2 threads are no longer supported in the topology
and the ``-smp`` command line argument accepts only ``threads=1``.

Prerequisites
-------------

To use CPU topology a Linux QEMU/KVM machine providing the CPU topology facility
(STFLE bit 11) is required.

However, since this facility has been enabled by default in an early version
of QEMU, we use a capability, ``KVM_CAP_S390_CPU_TOPOLOGY``, to notify KVM
that QEMU is supporting CPU topology.

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

The CPU topology, can be specified on the QEMU command line
with the ``-smp`` or the ``-device`` QEMU command arguments
without using any new attributes.
In this case, the topology will be calculated by simply adding
to the topology the cores based on the core-id starting with
core-0 at position 0 of socket-0, book-0, drawer-0 with default
modifier attributes: horizontal polarity and no dedication.

In the following machine we define 8 sockets with 4 cores each.
Note that s390x QEMU machines do not implement multithreading.

.. code-block:: bash

  $ qemu-system-s390x -m 2G \
    -cpu gen16b,ctop=on \
    -smp cpus=5,sockets=8,cores=4,maxcpus=32 \
    -device host-s390x-cpu,core-id=14 \

New CPUs can be plugged using the device_add hmp command like in:

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

Polarity and dedication
-----------------------

Polarity can be of two types: horizontal or vertical.

The horizontal polarization specifies that all guest's vCPUs get
almost the same amount of provisioning of real CPU by the host.

The vertical polarization specifies that guest's vCPU can get
different real CPU provisions:

- a vCPU with Vertical high entitlement specifies that this
  vCPU gets 100% of the real CPU provisioning.

- a vCPU with Vertical medium entitlement specifies that this
  vCPU shares the real CPU with other vCPUs.

- a vCPU with Vertical low entitlement specifies that this
  vCPU only get real CPU provisioning when no other vCPU need it.

In the case a vCPU with vertical high entitlement does not use
the real CPU, the unused "slack" can be dispatched to other vCPU
with medium or low entitlement.

A subsystem reset puts all vCPU of the configuration into the
horizontal polarization.

The admin specifies the dedicated bit when the vCPU is dedicated
to a single real CPU.

As for the Linux admin, the dedicated bit is an indication on the
affinity of a vCPU for a real CPU while the entitlement indicates the
sharing or exclusivity of use.

Defining the topology on command line
-------------------------------------

The topology can be defined entirely during the CPU definition,
with the exception of CPU 0 which must be defined with the -smp
argument.

For example, here we set the position of the cores 1,2,3 on
drawer 1, book 1, socket 2 and cores 0,9 and 14 on drawer 0,
book 0, socket 0 with all horizontal polarity and not dedicated.
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
    -device gen16b-s390x-cpu,core-id=4,dedicated=on,polarity=3 \

QAPI interface for topology
---------------------------

Let's start QEMU with the following command:

.. code-block:: bash

 sudo /usr/local/bin/qemu-system-s390x \
    -enable-kvm \
    -cpu z14,ctop=on \
    -smp 1,drawers=3,books=3,sockets=2,cores=2,maxcpus=36 \
    \
    -device z14-s390x-cpu,core-id=19,polarity=3 \
    -device z14-s390x-cpu,core-id=11,polarity=1 \
    -device z14-s390x-cpu,core-id=112,polarity=3 \
   ...

and see the result when using of the QAPI interface.

addons to query-cpus-fast
+++++++++++++++++++++++++

The command query-cpus-fast allows the admin to query the topology
tree and modifiers for all configured vCPU.

.. code-block:: QMP

 -> { "execute": "query-cpus-fast" }
 {
  "return": [
    {
      "dedicated": false,
      "thread-id": 3631238,
      "props": {
        "core-id": 0,
        "socket-id": 0,
        "drawer-id": 0,
        "book-id": 0
      },
      "cpu-state": "operating",
      "qom-path": "/machine/unattached/device[0]",
      "polarity": 2,
      "cpu-index": 0,
      "target": "s390x"
    },
    {
      "dedicated": false,
      "thread-id": 3631248,
      "props": {
        "core-id": 19,
        "socket-id": 9,
        "drawer-id": 0,
        "book-id": 2
      },
      "cpu-state": "operating",
      "qom-path": "/machine/peripheral-anon/device[0]",
      "polarity": 3,
      "cpu-index": 19,
      "target": "s390x"
    },
    {
      "dedicated": false,
      "thread-id": 3631249,
      "props": {
        "core-id": 11,
        "socket-id": 5,
        "drawer-id": 0,
        "book-id": 1
      },
      "cpu-state": "operating",
      "qom-path": "/machine/peripheral-anon/device[1]",
      "polarity": 1,
      "cpu-index": 11,
      "target": "s390x"
    },
    {
      "dedicated": true,
      "thread-id": 3631250,
      "props": {
        "core-id": 112,
        "socket-id": 56,
        "drawer-id": 3,
        "book-id": 14
      },
      "cpu-state": "operating",
      "qom-path": "/machine/peripheral-anon/device[2]",
      "polarity": 3,
      "cpu-index": 112,
      "target": "s390x"
    }
  ]
 }

x-set-cpu-topology
++++++++++++++++++

The command x-set-cpu-topology allows the admin to modify the topology
tree or the topology modifiers of a vCPU in the configuration.

.. code-block:: QMP

 -> { "execute": "x-set-cpu-topology",
      "arguments": {
         "core": 11,
         "socket": 0,
         "book": 0,
         "drawer": 0,
         "polarity": 0,
         "dedicated": false
      }
    }
 <- {"return": {}}


event CPU_POLARITY_CHANGE
+++++++++++++++++++++++++

When a guest is requesting a modification of the polarity,
QEMU sends a CPU_POLARITY_CHANGE event.

When requesting the change, the guest only specifies horizontal or
vertical polarity.
The dedication and fine grain vertical entitlement depends on admin
to set according to its response to this event.

Note that a vertical polarized dedicated vCPU can only have a high
entitlement, this gives 6 possibilities for a vCPU polarity:

- Horizontal
- Horizontal dedicated
- Vertical low
- Vertical medium
- Vertical high
- Vertical high dedicated

Example of the event received when the guest issues the CPU instruction
Perform Topology Function PTF(0) to request an horizontal polarity:

.. code-block:: QMP

 <- { "event": "CPU_POLARITY_CHANGE",
      "data": { "polarity": 0 },
      "timestamp": { "seconds": 1401385907, "microseconds": 422329 } }


