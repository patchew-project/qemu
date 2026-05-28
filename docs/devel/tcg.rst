.. _tcg_internals:

====================
Translator Internals
====================

QEMU is a dynamic translator. When it first encounters a piece of code,
it converts it to the host instruction set. Usually dynamic translators
are very complicated and highly CPU dependent. QEMU uses some tricks
which make it relatively easily portable and simple while achieving good
performances.

QEMU's dynamic translation backend is called TCG, for "Tiny Code
Generator". For more information, please take a look at :ref:`tcg-ops-ref`.

The translation process occurs in several distinct passes:

1. **Front-end**: Guest instructions are parsed (often using the
   `decodetree <Instruction Decoding (decodetree)_>`_ tool) and converted
   into target-independent TCG Intermediate Representation (IR) opcodes.
2. **Optimization**: TCG performs passes such as constant folding, liveness
   analysis, and dead code elimination on the IR.
3. **Back-end**: The optimized IR is converted by a host-specific code
   generator into native instructions for the host CPU.

The following sections outline some notable features and implementation
details of QEMU's dynamic translator.

CPU state optimisations
-----------------------

The target CPUs have many internal states which change the way they
evaluate instructions. In order to achieve a good speed, the
translation phase considers that some state information of the virtual
CPU cannot change in it. The state is recorded in the Translation
Block (TB). If the state changes (e.g. privilege level), a new TB will
be generated and the previous TB won't be used anymore until the state
matches the state recorded in the previous TB. The same idea can be applied
to other aspects of the CPU state.  For example, on x86, if the SS,
DS and ES segments have a zero base, then the translator does not even
generate an addition for the segment base.

Direct block chaining
---------------------

After each translated basic block is executed, QEMU uses the simulated
Program Counter (PC) and other CPU state information (such as the CS
segment base value) to find the next basic block.

In its simplest, less optimized form, this is done by exiting from the
current TB, going through the TB epilogue, and then back to the
main loop. That’s where QEMU looks for the next TB to execute,
translating it from the guest architecture if it isn’t already available
in memory. Then QEMU proceeds to execute this next TB, starting at the
prologue and then moving on to the translated instructions.

In :ref:`MTTCG` mode, each guest CPU is emulated by a separate host thread.
TCG ensures memory consistency by inserting memory barrier (``mb``) opcodes
for guest instructions with ordering side effects. Direct block chaining
across page boundaries is restricted to ensure that changes to memory
mappings in one thread are correctly handled by others.

Exiting from the TB this way will cause the ``cpu_exec_interrupt()``
callback to be re-evaluated before executing additional instructions.
It is mandatory to exit this way after any CPU state changes that may
unmask interrupts.

In order to accelerate the cases where the TB for the new
simulated PC is already available, QEMU has mechanisms that allow
multiple TBs to be chained directly, without having to go back to the
main loop as described above. These mechanisms are:

``lookup_and_goto_ptr``
^^^^^^^^^^^^^^^^^^^^^^^

Calling ``tcg_gen_lookup_and_goto_ptr()`` will emit a call to
``helper_lookup_tb_ptr``. This helper will look for an existing TB that
matches the current CPU state. If the destination TB is available its
code address is returned, otherwise the address of the JIT epilogue is
returned. The call to the helper is always followed by the tcg ``goto_ptr``
opcode, which branches to the returned address. In this way, we either
branch to the next TB or return to the main loop.

``goto_tb + exit_tb``
^^^^^^^^^^^^^^^^^^^^^

The translation code usually implements branching by performing the
following steps:

1. Call ``tcg_gen_goto_tb()`` passing a jump slot index (either 0 or 1)
   as a parameter.

2. Emit TCG instructions to update the CPU state with any information
   that has been assumed constant and is required by the main loop to
   correctly locate and execute the next TB. For most guests, this is
   just the PC of the branch destination, but others may store additional
   data. The information updated in this step must be inferable from both
   ``cpu_get_tb_cpu_state()`` and ``cpu_restore_state()``.

3. Call ``tcg_gen_exit_tb()`` passing the address of the current TB and
   the jump slot index again.

Step 1, ``tcg_gen_goto_tb()``, will emit a ``goto_tb`` TCG
instruction that later on gets translated to a jump to an address
associated with the specified jump slot. Initially, this is the address
of step 2's instructions, which update the CPU state information. Step 3,
``tcg_gen_exit_tb()``, exits from the current TB returning a tagged
pointer composed of the last executed TB’s address and the jump slot
index.

The first time this whole sequence is executed, step 1 simply jumps
to step 2. Then the CPU state information gets updated and we exit from
the current TB. As a result, the behavior is very similar to the less
optimized form described earlier in this section.

Next, the main loop looks for the next TB to execute using the
current CPU state information (creating the TB if it wasn’t already
available) and, before starting to execute the new TB’s instructions,
patches the previously executed TB by associating one of its jump
slots (the one specified in the call to ``tcg_gen_exit_tb()``) with the
address of the new TB.

The next time this previous TB is executed and we get to that same
``goto_tb`` step, it will already be patched (assuming the destination TB
is still in memory) and will jump directly to the first instruction of
the destination TB, without going back to the main loop.

For the ``goto_tb + exit_tb`` mechanism to be used, the following
conditions need to be satisfied:

* The change in CPU state must be constant, e.g., a direct branch and
  not an indirect branch.

* The direct branch cannot cross a page boundary. Memory mappings
  may change, causing the code at the destination address to change.

Note that, on step 3 (``tcg_gen_exit_tb()``), in addition to the
jump slot index, the address of the TB just executed is also returned.
This address corresponds to the TB that will be patched; it may be
different than the one that was directly executed from the main loop
if the latter had already been chained to other TBs.

Self-modifying code and translated code invalidation
----------------------------------------------------

Self-modifying code is a special challenge in x86 emulation because no
instruction cache invalidation is signaled by the application when code
is modified.

User-mode emulation marks a host page as write-protected (if it is
not already read-only) every time translated code is generated for a
basic block.  Then, if a write access is done to the page, Linux raises
a SEGV signal. QEMU then invalidates all the translated code in the page
and enables write accesses to the page.  For system emulation, write
protection is achieved through the software MMU.

Correct translated code invalidation is done efficiently by maintaining
a linked list of every translated block contained in a given page. Other
linked lists are also maintained to undo direct block chaining.

On RISC targets, correctly written software uses memory barriers and
cache flushes, so some of the protection above would not be
necessary. However, QEMU still requires that the generated code always
matches the target instructions in memory in order to handle
exceptions correctly.

Exception support
-----------------

longjmp() is used when an exception such as division by zero is
encountered.

The host SIGSEGV and SIGBUS signal handlers are used to get invalid
memory accesses.  QEMU keeps a map from host program counter to
target program counter, and looks up where the exception happened
based on the host program counter at the exception point.

On some targets, some bits of the virtual CPU's state are not flushed to the
memory until the end of the translation block.  This is done for internal
emulation state that is rarely accessed directly by the program and/or changes
very often throughout the execution of a translation block---this includes
condition codes on x86, delay slots on SPARC, conditional execution on
Arm, and so on.  This state is stored for each target instruction, and
looked up on exceptions.

MMU emulation
-------------

For system emulation QEMU uses a software MMU. In that mode, the MMU
virtual to physical address translation is done at every memory
access.

QEMU uses an address translation cache (TLB) to speed up the translation.
The software MMU partitions accesses into a **TLB fast-path** and a
**TLB slow-path**. The fast-path handles RAM and ROM areas, where the TLB
provides the direct offset between guest virtual addresses and host memory.
If an access does not match a fast-path entry, it falls through to the
slow-path, which calls C helper functions to handle MMIO device emulation.

In order to avoid flushing the translated code each time the MMU
mappings change, all caches in QEMU are physically indexed.  This
means that each basic block is indexed with its physical address.

In order to avoid invalidating the basic block chain when MMU mappings
change, chaining is only performed when the destination of the jump
shares a page with the basic block that is performing the jump.

The MMU can also distinguish RAM and ROM memory areas from MMIO memory
areas.  Access is faster for RAM and ROM because the translation cache also
hosts the offset between guest address and host memory.  Accessing MMIO
memory areas instead calls out to C code for device emulation.
Finally, the MMU helps tracking dirty pages and pages pointed to by
translation blocks.

Register Allocation and Liveness
--------------------------------

During the translation phase, guest instructions are converted into TCG IR
using an **unlimited number of temporaries (TEMPs)**.
This allows guest translators to express logic without being constrained
by the finite register set of the host CPU.

To resolve these TEMPs into physical registers, TCG performs two passes:

1. **Liveness Analysis**: This pass determines the "live range" of each
   temporary within a basic block. By identifying when a variable
   becomes "dead" (i.e., its value is no longer needed), TCG can suppress
   redundant moves and remove instructions that compute unused results.
2. **Register Allocation**: The Global Register Allocator maps live TEMPs
   to host physical registers. Fixed globals, such as the pointer
   to the CPU architecture state (``cpu_env``), are often permanently
   held in host registers to minimize memory traffic during execution.

Vector/SIMD Internal Strategy
-----------------------------

TCG supports SIMD operations through a set of generic vector instructions
(e.g., ``add_vec``, ``shli_vec``) parameterized by vector length and element
size. The length is specified as a ``TCGType`` (V64, V128, or V256), and the
element size is given in log2 8-bit units.

The internal strategy relies on the backend mapping these generic opcodes
to native host SIMD instructions, such as x86 AVX or ARM NEON. If the host
backend does not support a specific vector operation  or length, TCG's
expansion layer automatically decomposes the opcode into smaller supported
vector sizes or standard integer operations.

Deterministic Execution (icount)
--------------------------------

The :ref:`icount` mechanism provides deterministic execution by ensuring
that each Translation Block executes a fixed number of instructions. This
is essential for features like record/replay and deterministic virtual time,
where instruction counts serve as the system clock.

Instrumentation and Plugins
---------------------------

:ref:`TCG Plugins` provide a mechanism for runtime instrumentation. Opcodes
like ``plugin_cb`` and ``plugin_mem_cb`` are inserted during translation to
trigger callbacks in external modules, allowing analysis of instruction
execution or memory access.

Instruction Decoding (decodetree)
---------------------------------

The first step of the translation process is converting a raw bitstream of
guest instructions into a structured format that the translator can process.
QEMU simplifies this using the ``decodetree.py`` script, which generates C
code decoders from a domain-specific language defined in ``.decode`` files.

The decodetree tool allows developers to define instruction **patterns**
based on a bitmask and fixed bits. When a match is found, the generated
decoder automatically  extracts defined **fields** (such as registers or
immediates) and passes  them to a manually written translation function.

This declarative approach drastically reduces the amount of error-prone
manual bit-shifting and nested "if-else" logic required in guest translators.

For detailled implementation see :ref:`decodetree`.

Profiling JITted code
---------------------

The Linux ``perf`` tool will treat all JITted code as a single block as
unlike the main code it can't use debug information to link individual
program counter samples with larger functions. To overcome this
limitation you can use the ``-perfmap`` or the ``-jitdump`` option to generate
map files. ``-perfmap`` is lightweight and produces only guest-host mappings.
``-jitdump`` additionally saves JITed code and guest debug information (if
available); its output needs to be integrated with the ``perf.data`` file
before the final report can be viewed.

.. code::

  perf record $QEMU -perfmap $REMAINING_ARGS
  perf report

  perf record -k 1 $QEMU -jitdump $REMAINING_ARGS
  DEBUGINFOD_URLS= perf inject -j -i perf.data -o perf.data.jitted
  perf report -i perf.data.jitted

Note that qemu-system generates mappings only for ``-kernel`` files in ELF
format.
