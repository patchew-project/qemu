QAPI interface for S390 CPU topology
====================================

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

Addons to query-cpus-fast
-------------------------

The command query-cpus-fast allows to query the topology tree and
modifiers for all configured vCPUs.

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


QAPI command: set-cpu-topology
------------------------------

The command set-cpu-topology allows to modify the topology tree
or the topology modifiers of a vCPU in the configuration.

.. code-block:: QMP

    { "execute": "set-cpu-topology",
      "arguments": {
         "core-id": 11,
         "socket-id": 0,
         "book-id": 0,
         "drawer-id": 0,
         "entitlement": "low",
         "dedicated": false
      }
    }
    {"return": {}}

The core-id parameter is the only non optional parameter and every
unspecified parameter keeps its previous value.

QAPI event CPU_POLARIZATION_CHANGE
----------------------------------

When a guest is requests a modification of the polarization,
QEMU sends a CPU_POLARIZATION_CHANGE event.

When requesting the change, the guest only specifies horizontal or
vertical polarization.
It is the job of the upper layer to set the dedication and fine grained
vertical entitlement in response to this event.

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

    { "event": "CPU_POLARIZATION_CHANGE",
      "data": { "polarization": 0 },
      "timestamp": { "seconds": 1401385907, "microseconds": 422329 } }
