CPU Topology on s390x
=====================

CPU Topology on S390x provides up to 5 levels of topology containers:
nodes, drawers, books, sockets and CPUs.
While the higher level containers, Containers Topology List Entries,
(Containers TLE) define a tree hierarchy, the lowest level of topology
definition, the CPU Topology List Entry (CPU TLE), provides the placement
of the CPUs inside the parent container.

Currently QEMU CPU topology uses a single level of container: the sockets.

For backward compatibility, threads can be declared on the ``-smp`` command
line. They will be seen as CPUs by the guest as long as multithreading
is not really supported by QEMU for S390.

Beside the topological tree, S390x provides 3 CPU attributes:
- CPU type
- polarity entitlement
- dedication

Prerequisites
-------------

To use CPU Topology a Linux QEMU/KVM machine providing the CPU Topology facility
(STFLE bit 11) is required.

However, since this facility has been enabled by default in an early version
of QEMU, we use a capability, ``KVM_CAP_S390_CPU_TOPOLOGY``, to notify KVM
QEMU use of the CPU Topology.

Enabling CPU topology
---------------------

Currently, CPU topology is only enabled in the host model.

Enabling CPU topology in a CPU model is done by setting the CPU flag
``ctop`` to ``on`` like in:

.. code-block:: bash

   -cpu gen16b,ctop=on

Having the topology disabled by default allows migration between
old and new QEMU without adding new flags.

Default topology usage
----------------------

The CPU Topology, can be specified on the QEMU command line
with the ``-smp`` or the ``-device`` QEMU command arguments
without using any new attributes.
In this case, the topology will be calculated by simply adding
to the topology the cores based on the core-id starting with
core-0 at position 0 of socket-0, book-0, drawer-0 with default
modifier attributes: horizontal polarity and no dedication.

In the following machine we define 8 sockets with 4 cores each.
Note that S390 QEMU machines do not implement multithreading.

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

Note that the core ID is machine wide and the CPU TLE masks provided
by the STSI instruction will be written in a big endian mask:

* in socket 0: 0xf000000000000000 (core id 0,1,2,3)
* in socket 1: 0x0800000000000000 (core id 4)
* in socket 2: 0x0040000000000000 (core id 9)
* in socket 3: 0x0002000000000000 (core id 14)

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

Polarity and dedication
-----------------------

Polarity can be of two types: horizontal or vertical.

The horizontal polarization specifies that all guest's vCPUs get
almost the same amount of provisioning of real CPU by the host.

The vertical polarization specifies that guest's vCPU can get
different  real CPU provisions:

- a vCPU with Vertical high entitlement specifies that this
  vCPU gets 100% of the real CPU provisioning.

- a vCPU with Vertical medium entitlement specifies that this
  vCPU shares the real CPU with other vCPU.

- a vCPU with Vertical low entitlement specifies that this
  vCPU only get real CPU provisioning when no other vCPU need it.

In the case a vCPU with vertical high entitlement does not use
the real CPU, the unused "slack" can be dispatched to other vCPU
with medium or low entitlement.

The host indicates to the guest how the real CPU resources are
provided to the vCPUs through the SYSIB with two polarity bits
inside the CPU TLE.

Bits d - Polarization
0 0      Horizontal
0 1      Vertical low entitlement
1 0      Vertical medium entitlement
1 1      Vertical high entitlement

A subsystem reset puts all vCPU of the configuration into the
horizontal polarization.

The admin specifies the dedicated bit when the vCPU is dedicated
to a single real CPU.

As for the Linux admin, the dedicated bit is an indication on the
affinity of a vCPU for a real CPU while the entitlement indicates the
sharing or exclusivity of use.

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
    -device z14-s390x-cpu,core-id=12,polarity=3 \
   ...

and see the result when using of the QAPI interface.

query-topology
+++++++++++++++

The command cpu-topology allows the admin to query the topology
tree and modifier for all configured vCPU.

.. code-block:: QMP

 -> { "execute": "query-topology" }
    {"return":
        [
            {
            "origin": 0,
            "dedicated": false,
            "book": 0,
            "socket": 0,
            "drawer": 0,
            "polarity": 0,
            "mask": "0x8000000000000000"
            },
            {
                "origin": 0,
                "dedicated": false,
                "book": 2,
                "socket": 1,
                "drawer": 0,
                "polarity": 1,
                "mask": "0x0010000000000000"
            },
            {
                "origin": 0,
                "dedicated": false,
                "book": 0,
                "socket": 0,
                "drawer": 1,
                "polarity": 3,
                "mask": "0x0008000000000000"
            },
            {
                "origin": 0,
                "dedicated": false,
                "book": 1,
                "socket": 1,
                "drawer": 1,
                "polarity": 3,
                "mask": "0x0000100000000000"
            }
        ]
    }

change-topology
+++++++++++++++

The command change-topology allows the admin to modify the topology
tree or the topology modifiers of a vCPU in the configuration.

.. code-block:: QMP

 -> { "execute": "change-topology",
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


event POLARITY_CHANGE
+++++++++++++++++++++

When a guest is requesting a modification of the polarity,
QEMU sends a POLARITY_CHANGE event.

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

Example of the event received when the guest issues PTF(0) to request
an horizontal polarity:

.. code-block:: QMP

 <- { "event": "POLARITY_CHANGE",
      "data": { "polarity": 0 },
      "timestamp": { "seconds": 1401385907, "microseconds": 422329 } }


