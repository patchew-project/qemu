==================================
QEMU TCG PMU-EBB support for PPC64
==================================

Introduction
============

QEMU version 6.2 introduces an EBB (Event-Based Branch) implementation
for PPC64 TCG guests. It was introduced together with a simple PMU
(Performance Monitor Unit) implementation which was only introduced
as a means to validate EBB using the Linux kernel selftests located
in the kernel tree at tools/testing/selftests/powerpc/pmu/ebb.

The goal of this document is to give a brief explanation of what
to expect, and more important, what to not expect from this existing
PMU implementation.


EBB support
-----------

The existing EBB support can be summarized as follows:

 - all bits from BESCR are implemented;
 - rfebb instruction is implemented as the mnemonic 'rfebb 1', i.e. the
 instruction will always set BESCR_GE;
 - support for both Performance Monitor and External event-based exceptions
 are included, although there is no code that triggers an external exception
 at this moment.


PMU support
-----------

The existing PMU logic is capable of counting instructions (perf event
PM_INST_CMPL) and cycles (perf event PM_CYC) using QEMU's icount
framework. A handful of PM_STALL events were added as fixed ratio of
the total cycles as a means to enable one of the EBB tests.

Everything that is not mentioned above is not supported in the PMU. Most
notably:

 - reading unfrozen (running) PMCs will return their last set value. The PMCs
 are only updated after they're frozen;
 - no MMCR2 and MMCRA support. The registers can be read and written at will,
 but the PMU will ignore it;
 - as a consequence of not supporting MMCRA, no random events and no threshold
 event counters are enabled;
 - no form of BHRB support is implemented;
 - several MMCR0 bits are not supported;
 - access control of the PMCs is only partially done. For example, setting
 MMCR0_PMCC to 0b11 will not exclude PMC5 and PMC6 from the PMU.


icount usage
------------

The development of both the PMU and EBB support were tested with icount shift
zero with alignment, e.g. this command line option:

``-icount shift=0,align=on``

Different 'shift' options will degrade the performance of the PMU tests and some
EBB tests that relies on small count error margins (e.g. 'count_instructions').

Running PMU and EBB tests without any icount support will not give reliable
results due to how the instructions and cycles relies on icount to work.

It's also worth mentioning that all these icount restrictions and conditions
are exclusive to the PMU logic. The Event-Based Branch code does not rely on
the icount availability or configuration to work.
