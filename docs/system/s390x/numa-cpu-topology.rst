NUMA CPU Topology on S390x
==========================

IBM S390 provides a complex CPU architecture with several cache levels.
Using NUMA with the CPU topology is a way to let the guest optimize his
accesses to the main memory.

The QEMU smp parameter for S390x allows to specify 4 NUMA levels:
core, socket, drawer and book and these levels are available for
the numa parameter too.


Prerequisites
-------------

To take advantage of the CPU topology, KVM must give support for the
Perform Topology Function and to the Store System Information instructions
as indicated by the Perform CPU Topology facility (stfle bit 11).

If those requirements are met, the capability ``KVM_CAP_S390_CPU_TOPOLOGY``
will indicate that KVM can support CPU Topology on that LPAR.


Using CPU Topology in QEMU for S390x
------------------------------------


QEMU -smp parameter
~~~~~~~~~~~~~~~~~~~

With -smp QEMU provides the user with the possibility to define
a Topology based on ::

  -smp [[cpus=]n][,maxcpus=maxcpus][,drawers=drawers][,books=books] \
       [,sockets=sockets][,cores=cores]

The topology reported to the guest in this situation will provide
n cpus of a maximum of maxcpus cpus, filling the topology levels one by one
starting with CPU0 being the first CPU on drawer[0] book[0] socket[0].

For example ``-smp 5,books=2,sockets=2,cores=2`` will provide ::

  drawer[0]--+--book[0]--+--socket[0]--+--core[0]-CPU0
             |           |             |
             |           |             +--core[1]-CPU1
             |           |
             |           +--socket[1]--+--core[0]-CPU2
             |                         |
             |                         +--core[1]-CPU3
             |
             +--book[1]--+--socket[0]--+--core[0]-CPU4


Note that the thread parameter can not be defined on S390 as it
has no representation on the CPU topology.


QEMU -numa parameter
~~~~~~~~~~~~~~~~~~~

With -numa QEMU provides the user with the possibility to define
the Topology in a non uniform way ::

  -smp [[cpus=]n][,maxcpus=maxcpus][,drawers=drawers][,books=books] \
       [,sockets=sockets][,cores=cores]
  -numa node[,memdev=id][,cpus=firstcpu[-lastcpu]][,nodeid=node][,initiator=initiator]
  -numa cpu,node-id=node[,drawer-id=x][,book-id=x][,socket-id=x][,core-id=y]

The topology reported to the guest in this situation will provide
n cpus of a maximum of maxcpus cpus, and the topology entries will be

- if there is less cpus than specified by the -numa arguments
  the topology will be build by filling the numa definitions
  starting with the lowest node.

- if there is more cpus than specified by the -numa argument
  the numa specification will first be fulfilled and the remaining
  CPU will be assigned to unassigned slots starting with the
  core 0 on socket 0.

- a CPU declared with -device does not count inside the ncpus parameter
  of the -smp argument and will be added on the topology based on
  its core ID.

For example  ::

  -smp 3,drawers=8,books=2,sockets=2,cores=2,maxcpus=64
  -object memory-backend-ram,id=mem0,size=10G
  -numa node,nodeid=0,memdev=mem0
  -numa node,nodeid=1
  -numa node,nodeid=2
  -numa cpu,node-id=0,drawer-id=0
  -numa cpu,node-id=1,socket-id=9
  -device host-s390x-cpu,core-id=19

Will provide the following topology ::

  drawer[0]--+--book[0]--+--socket[0]--+--core[0]-CPU0
                         |             |
                         |             +--core[1]-CPU1
                         |
                         +--socket[1]--+--core[0]-CPU2

  drawer[2]--+--book[0]--+--socket[1]--+--core[1]-CPU19


S390 NUMA specificity
---------------------

Heterogene Memory Attributes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The S390 topology implementation does not use ACPI HMAT to specify the
cache size and bandwidth between nodes.

Memory device
~~~~~~~~~~~~~

When using NUMA S390 needs a memory device to be associated with
the nodes definitions. As we do not use HMAT, it has little sense
to assign memory to each node and one should assign all memory to
a node without CPU and use other nodes to define the CPU Topology.

Exemple ::

  -object memory-backend-ram,id=mem0,size=10G
  -numa node,nodeid=0,memdev=mem0


CPUs
~~~~

In the S390 topology we do not use threads and the first topology
level is the core.
The number of threads can no be defined for S390 and is always equal to 1.

When using NUMA, QEMU issues a warning for CPUS not assigned to nodes.
The S390 topology will silently assign unassigned CPUs to the topology
searching for free core starting on the first core of the first socket
in the first book.
This is of course advised to assign all possible CPUs to nodes to
guaranty future compatibility.


The topology provided to the guest
----------------------------------

The guest , when the CPU Topology is available as indicated by the
Perform CPU Topology facility (stfle bit 11) may use two instructions
to retrieve the CPU topology and optimize its CPU scheduling:

- PTF (Perform Topology function) which will give information
  about a change in the CPU Topology, that is a change in the
  result of the STSI(15,1,2) instruction.

- STSI (Stote System Information) with parameters (15,1,2)
  to retrieve the CPU Topology.

Exemple ::

  -smp 3,drawers=8,books=2,sockets=2,cores=2,maxcpus=64
  -object memory-backend-ram,id=mem0,size=10G
  -numa node,nodeid=0,memdev=mem0
  -numa node,nodeid=1
  -numa node,nodeid=2
  -numa cpu,node-id=1,drawer-id=0
  -numa cpu,node-id=2,socket-id=9
  -device host-s390x-cpu,core-id=19

Formated result for STSI(15,1,2) showing the 6 different levels
with:
- levels 2 (socket) and 1 (core) used.
- 3 sockets with a CPU mask for CPU type 3, non dedicated and
  with horizontal polarization.
- The first socket contains 2 cores as specified by the -smp argument
- The second socket contains the 3rd core defined by the -smp argument
- both these sockets belong to drawer-id=0 and to node-1
- The third socket hold the CPU with core-id 19 assigned to socket-id 9
  and to node-2

Here the kernel view ::

  mag[6] = 0
  mag[5] = 0
  mag[4] = 0
  mag[3] = 0
  mag[2] = 32
  mag[1] = 2
  MNest  = 2
  socket: 1 0
  cpu type 03  d: 0 pp: 0
  origin : 0000
  mask   : c000000000000000

  socket: 1 1
  cpu type 03  d: 0 pp: 0
  origin : 0000
  mask   : 2000000000000000

  socket: 1 9
  cpu type 03  d: 0 pp: 0
  origin : 0000
  mask   : 0000100000000000

And the admin view ::

  # lscpu -e
  CPU NODE DRAWER BOOK SOCKET CORE L1d:L1i:L2d:L2i ONLINE CONFIGURED POLARIZATION ADDRESS
  0   0    0      0    0      0    0:0:0:0         yes    yes        horizontal   0
  1   0    0      0    0      1    1:1:1:1         yes    yes        horizontal   1
  2   0    0      0    1      2    2:2:2:2         yes    yes        horizontal   2
  3   0    1      1    2      3    3:3:3:3         yes    yes        horizontal   19


Hotplug with NUMA
-----------------

Using the core-id the topology is automatically calculated to put the core
inside the right socket.

Example::

  (qemu) device_add host-s390x-cpu,core-id=8

  # lscpu -e
  CPU NODE DRAWER BOOK SOCKET CORE L1d:L1i:L2d:L2i ONLINE CONFIGURED POLARIZATION ADDRESS
  0   0    0      0    0      0    0:0:0:0         yes    yes        horizontal   0
  1   0    0      0    0      1    1:1:1:1         yes    yes        horizontal   1
  2   0    0      0    1      2    2:2:2:2         yes    yes        horizontal   2
  3   0    1      1    2      3    3:3:3:3         yes    yes        horizontal   19
  4   -    -      -    -      -    :::             no     yes        horizontal   8

  # chcpu -e 4
  CPU 4 enabled
  # lscpu -e
  CPU NODE DRAWER BOOK SOCKET CORE L1d:L1i:L2d:L2i ONLINE CONFIGURED POLARIZATION ADDRESS
  0   0    0      0    0      0    0:0:0:0         yes    yes        horizontal   0
  1   0    0      0    0      1    1:1:1:1         yes    yes        horizontal   1
  2   0    0      0    1      2    2:2:2:2         yes    yes        horizontal   2
  3   0    1      1    2      3    3:3:3:3         yes    yes        horizontal   19
  4   0    2      2    3      4    4:4:4:4         yes    yes        horizontal   8

One can see that the userland tool reports serials IDs which do not correspond
to the firmware IDs but does however report the new CPU on it's own socket.

The result seen by the kernel looks like ::

  mag[6] = 0
  mag[5] = 0
  mag[4] = 0
  mag[3] = 0
  mag[2] = 32
  mag[1] = 2
  MNest  = 2
  00 - socket: 1 0
  cpu type 03  d: 0 pp: 0
  origin : 0000
  mask   : c000000000000000

  socket: 1 1
  cpu type 03  d: 0 pp: 0
  origin : 0000
  mask   : 2000000000000000

  socket: 1 9
  cpu type 03  d: 0 pp: 0
  origin : 0000
  mask   : 0000100000000000

  socket: 1 4
  cpu type 03  d: 0 pp: 0
  origin : 0000
  mask   : 0080000000000000
