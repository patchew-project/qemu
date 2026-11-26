.. SPDX-License-Identifier: GPL-2.0-or-later

.. _Hexagon Emulation:

Hexagon CPU architecture support
================================

QEMU's TCG emulation includes support for v65, v66, v67, v68, v69, v71, v73,
v75, v77, v79, v81, v83, v85.
It also has support for the following architecture extensions:

- HVX (Hexagon Vector eXtensions)
- `HVX floating point <https://docs.qualcomm.com/doc/80-N2040-61/topic/hvx-floating-point.html>`_.
- HLX (see the `HLX reference manual
  <https://docs.qualcomm.com/doc/80-N3040-23/topic/hlx.html>`_).

User-DMA and HMX are not yet implemented.

Machine configurations also select the available HVX contexts, VTCM,
and HMX/HLX resources. See :doc:`machines` for the machine configurations.

For information on the specifics of the HVX extension, please refer
to the `Qualcomm Hexagon V79 HVX Programmer's Reference Manual
<https://docs.qualcomm.com/doc/80-N2040-61/>`_.

Emulation limits
----------------

The system models target functional execution.

HVX uses 128-byte vectors; 64-byte mode is not implemented.

See :doc:`../../devel/hexagon-sys` and
:doc:`../../devel/hexagon-l2vic` for system-register, TLB, and interrupt
details relevant to OS and firmware authors.

Performance Monitoring Unit (PMU)
----------------------------------

Every Hexagon system machine has a PMU with 12 event counters (``PMUCNT0``
to ``PMUCNT11``), selected through ``PMUEVTCFG``, ``PMUEVTCFG1``,
``PMUEVTCFG2`` and ``PMUCFG``. The ``GPMUCNT`` and ``UPMUCNT`` aliases are
readable when ``SSR.PE`` is set. Counting is enabled by ``SYSCFG.PM``; while
it is clear, no counting code is generated, so an idle PMU costs nothing.

Only events that follow from decoding and committing packets are counted:

- committed packets, in total and per hw thread
- scalar loads, stores and memops
- HVX packets and the HVX ALU, multiply, shift and permute instructions
- HLX packets, loads, stores and the HLX execution slots

When guests select other events the counter does not advance.

Limitations
^^^^^^^^^^^

- The hardware threads of a machine share one bank of counters. Events that
  have no per-thread variant are summed across all threads.
- ``PMUCFG`` thread mode and ``PMUSTID`` filtering are stored and read back
  but not acted on.

For the full event list and register layout, refer to the
`Hexagon V85 PMU Events reference
<https://docs.qualcomm.com/doc/80-N2040-60/topic/pmu-events.html>`_, the
`Hexagon V85 HVX PMU Events reference
<https://docs.qualcomm.com/doc/80-N2040-61/topic/hvx-pmu-events.html>`_ and,
for the HLX events, the `HLX reference manual
<https://docs.qualcomm.com/doc/80-N3040-23/topic/hlx.html>`_.
