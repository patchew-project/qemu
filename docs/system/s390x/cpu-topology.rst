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

Indicating the CPU topology to the Virtual Machine
--------------------------------------------------

The CPU Topology, can be specified on the QEMU command line
with the ``-smp`` or the ``-device`` QEMU command arguments.

In the following machine we define 8 sockets with 4 cores each.
Note that S390 QEMU machines do not implement multithreading.

.. code-block:: bash

  $ qemu-system-s390x -m 2G \
    -cpu gen16b,ctop=on \
    -smp cpus=5,sockets=8,cores=4,maxcpus=32 \
    -device host-s390x-cpu,core-id=14 \

New CPUs can be plugged using the device_add hmp command like in:

.. code-block:: bash

  (qemu) device_add host-s390x-cpu,core-id=9

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
by the STSI instruction will be writen in a big endian mask:

* in socket 0: 0xf0000000 (core id 0,1,2,3)
* in socket 1: 0x08000000 (core id 4)
* in socket 2: 0x00400000 (core id 9)
* in socket 3: 0x00020000 (core id 14)
