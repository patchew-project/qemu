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

Indicating the CPU topology to the Virtual Machine
--------------------------------------------------

The CPU Topology, can be specified on the QEMU command line
with the ``-smp`` or the ``-device`` QEMU command arguments.

Like in :

.. code-block:: sh
    -smp cpus=5,sockets=8,cores=2,threads=2,maxcpus=32
    -device host-s390x-cpu,core-id=14

New CPUs can be plugged using the device_add hmp command like in:

.. code-block:: sh
   (qemu) device_add host-s390x-cpu,core-id=9

The core-id defines the placement of the core in the topology by
starting with core 0 in socket 0 up to maxcpus.

In the example above:

* There are 5 CPUs provided to the guest with the ``-smp`` command line
  They will take the core-ids 0,1,2,3,4
  As we have 2 threads in 2 cores in a socket, we have 4 CPUs provided
  to the guest in socket 0, with core-ids 0,1,2,3.
  The last cpu, with core-id 4, will be on socket 1.

* the core with ID 14 provided by the ``-device`` command line will
  be placed in socket 3, with core-id 14

* the core with ID 9 provided by the ``device_add`` qmp command will
  be placed in socket 2, with core-id 9

Note that the core ID is machine wide and the CPU TLE masks provided
by the STSI instruction will be:

* in socket 0: 0xf0000000 (core id 0,1,2,3)
* in socket 1: 0x00400000 (core id 9)
* in socket 1: 0x00020000 (core id 14)

Migration
---------

For virtio-ccw machines older than s390-virtio-ccw-7.2, CPU Topoogy is
unavailable.

CPU topology is by default enabled for s390-virtio-ccw-7.2 and newer machines.

Disabling CPU topology can be done by setting the global option
``topology`` to ``off`` like in:

.. code-block:: sh
   -machine s390-ccw-virtio-7.2,accel=kvm,topology=off
