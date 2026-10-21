.. SPDX-License-Identifier: GPL-2.0-or-later

.. _riscv-hwtrace:

RISC-V Hardware Trace support for RISC-V CPUs
=============================================

QEMU implements a hardware tracing support based on the Efficient Trace
for RISC-V v2.0 specification (`etrace spec`_) and the RISC-V Trace Control
Interface Specification (`trace control spec`_).  Only a subset of all
available features are implemented at this point.  More features will being
added along the way, probably including support for N-trace support as well.

What is available at this moment as far as hardware model are concerned:

- e-trace supported added in the 'virt' RISC-V machine;
- support for all TCG RISC-V CPU models, i.e. all available CPUs are able
  to use the hw trace;
- topology: one trace encoder/trace ram sink pair per CPU;
- trace encoder has just the minimal bits specified in `trace control spec`_,
  section "Minimal Implementation";
- trace ramsink has SMEM support only;
- no trace funnel support.

A lot of considerations and simplications were made to adapt to the current
TCG RISC-V model we have in place:

- We do not have a pipeline model for QEMU RISC-V TCG CPUs.  This means
  that an instruction that is emitted will be retired shortly after.  There's
  no concurrent insns being executed in the same CPU thread;
- all trap packets are sent with 'thaddr' set to 1 because (1) we'll always
  know the trap EPC during a CPU interrupt and (2) there's no scenario where
  a second trap is fired while the handler of the first trap is still in
  flight because, as said above, we do not have a pipeline model in QEMU
  RISC-V and all insns are emitted and retired in order;
- irreport will never be set, i.e. irreport == updiscon, because we'll
  always retire one insns per cycle.  Likewise, irdepth is not implemented.

This emulation is compatible with the riscv-etrace-lib library located here:

https://github.com/riscv-software-src/riscv-etrace-lib.git

And some of the code being used to generated the packets were borrowed from
this library.  At this point there are no plans to make riscv-etrace-lib a
dependency for this emulation.

Encoding parameters
-------------------

Several assumptions and simplifications were made in the code, in particular
with the so called packet and itrace parameters used to write the mesasages.
The present emulation does not allow changing these parameters, i.e. there's
no way of changing the iaddress_width_p value.  In case there's enough demand
and use cases we'll consider creating encoding properties to make them
configurable.

Here is a list of the parameters being used in the encoding process:

============================ ===============
Param name                   Hardcoded value
============================ ===============
.packet.srcid_bytes_p        0
.packet.tstamp_bytes_p       0
.packet.type_width_p         0
.itrace.arch_p               0
.itrace.blocks_p             0
.itrace.bpred_size_p         5
.itrace.cache_size_p         0
.itrace.call_counter_size_p  1
.itrace.ctype_width_p        0
.itrace.context_width_p      0
.itrace.time_width_p         0
.itrace.ecause_width_p       6
.itrace.f0s_width_p          1
.itrace.filter_context_p     0
.itrace.filter_time_p        0
.itrace.filter_excint_p      0
.itrace.filter_privilege_p   0
.itrace.filter_tval_p        0
.itrace.iaddress_lsb_p       0
.itrace.iaddress_width_p     64
.itrace.iretire_width_p      0
.itrace.ilastsize_width_p    1
.itrace.itype_width_p        0
.itrace.nocontext_p          1
.itrace.notime_p             1
.itrace.privilege_width_p    3
.itrace.retires_p            1
.itrace.return_stack_size_p  1
.itrace.sijump_p             0
.itrace.impdef_width_p       0
============================ ===============


.. _etrace spec: https://github.com/riscv-non-isa/riscv-trace-spec/releases/download/v2.0-20250616/riscv-trace-spec-asciidoc.pdf
.. _trace control spec: https://github.com/riscv-non-isa/tg-nexus-trace/releases/download/1.0_Ratified/RISC-V-Trace-Control-Interface.pdf
