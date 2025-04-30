..
   Copyright (c) 2025 Lauterbach GmbH
   SPDX-License-Identifier: GPL-2.0-or-later


==========================
Multi-Core Debug (MCD) API
==========================

The Multi-Core Debug (MCD) API is a debug interface which is commonly provided
by emulators alongside other interfaces such as GDB and is specifically designed
for machine-level debugging.

For instance, to allow physical memory access through the GDB interface, QEMU
introduced a custom maintenance packet, as explained in :doc:`/system/gdb`.
With MCD, multiple memory spaces can be made available, which can be accessed
from a given CPU. In addition to ``MCD_MEM_SPACE_IS_REGISTERS`` and
``MCD_MEM_SPACE_IS_LOGICAL``, the types also include
``MCD_MEM_SPACE_IS_PHYSICAL``. Each memory and register accessed is initiated
through a transaction. Depending on the memory space identifier in the address,
the transaction accesses registers, logical memory (with MMU), or physical
memory.

Operations like accessing memory or registers, and controlling the execution
flow, are all performed with respect to an open core connection. As the
following diagram shows, cores are part of a hierarchical system structure which
can be directly mapped to QEMU's options:

.. code-block:: bash

  $ qemu-system-* -machine * -cpu * -smp * (...)

The resulting system can be visualized as follows::

    +----------------+      +----------+           +------+
    |     System     | 1  1 |  Device  | 1    -smp | Core |
    |                |------|          |-----------|      |
    | qemu-system-*  |      | -machine |           | -cpu |
    +----------------+      +----------+           +------+

To configure the build for MCD support:

.. code::

  ./configure --enable-mcd

Debugging via QAPI
------------------

Since the MCD API does not define a communication protocol, a QAPI schema
has been added to implement a remote procedure call mechanism.
Each function within the API corresponds to one QAPI command, ensuring a
one-to-one mapping between the API's functions and the QAPI commands.

Use the option ``-mcd dev`` to start MCD's QAPI monitor.

QAPI Reference
--------------

.. qapi-doc:: qapi/mcd.json

API Reference
-------------

.. kernel-doc:: mcd/mcd_api.h
