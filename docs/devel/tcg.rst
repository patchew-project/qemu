====================
Translator Internals
====================

QEMU is a dynamic translator. When it first encounters a piece of code,
it converts it to the host instruction set. Usually dynamic translators
are very complicated and highly CPU dependent. QEMU uses some tricks
which make it relatively easily portable and simple while achieving good
performances.

QEMU's dynamic translation backend is called TCG, for "Tiny Code
Generator". For more information, please take a look at ``tcg/README``.

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
current TB, going through the TB epilogue, and then back to the outer
execution loop. That’s where QEMU looks for the next TB to execute,
translating it from the guest architecture if it isn’t already available
in memory. Then QEMU proceeds to execute this next TB, starting at the
prologue and then moving on to the translated instructions.

In order to accelerate the most common cases where the TB for the new
simulated PC is already available, QEMU has mechanisms that allow
multiple TBs to be chained directly, without having to go back to the
outer execution loop as described above. These mechanisms are:

``lookup_and_goto_ptr``
^^^^^^^^^^^^^^^^^^^^^^^

On platforms that support the ``lookup_and_goto_ptr`` mechanism, calling
``tcg_gen_lookup_and_goto_ptr()`` will emit TCG instructions that call
a helper function to look for the destination TB, based on
the CPU state information. If the destination TB is available, a
``goto_ptr`` TCG instruction is emitted to jump directly to its first
instruction, skipping the epilogue - execution loop - prologue path.
If the destination TB is not available, the ``goto_ptr`` instruction
jumps to the epilogue, effectively exiting from the current TB and
going back to the execution loop.

On platforms that do not support this mechanism, the
``tcg_gen_lookup_and_goto_ptr()`` function will just use
``tcg_gen_exit_tb()`` to exit from the current TB.

``goto_tb + exit_tb``
^^^^^^^^^^^^^^^^^^^^^

On platforms that support this mechanism, the translation code usually
implements branching by performing the following steps:

1. Call ``tcg_gen_goto_tb()`` passing a jump slot index (either 0 or 1)
   as a parameter

2. Emit TCG instructions to update the CPU state information with the
   address of the next instruction to execute

3. Call ``tcg_gen_exit_tb()`` passing the address of the current TB and
   the jump slot index again

Step 1, ``tcg_gen_goto_tb()``, will emit a ``goto_tb`` TCG
instruction that later on gets translated to a jump to an address
associated with the specified jump slot. Initially, this is the address
of step 2's instructions, which update the CPU state information. Step 3,
``tcg_gen_exit_tb()``, exits from the current TB returning a tagged
pointer composed of the last executed TB’s address and the jump slot
index.

The first time this whole sequence is translated to target instructions
and executed, step 1 doesn’t do anything really useful, as it just jumps
to step 2. Then the CPU state information gets updated and we exit from
the current TB. As a result, the behavior is very similar to the less
optimized form described earlier in this section.

Next, the execution loop looks for the next TB to execute using the
current CPU state information (creating the TB if it wasn’t already
available) and, before starting to execute the new TB’s instructions,
tries to patch the previously executed TB by associating one of its jump
slots (the one specified in the call to ``tcg_gen_exit_tb()``) with the
address of the new TB.

The next time this previous TB is executed and we get to that same
``goto_tb`` step, it will already be patched (assuming the destination TB
is still in memory) and will jump directly to the first instruction of
the destination TB, without going back to the outer execution loop.
The most portable code patches TBs using indirect jumps. An indirect
jump makes it easier to make the jump target modification atomic. On some
host architectures (such as x86 and PowerPC), the ``JUMP`` opcode is
directly patched so that the block chaining has no overhead.

Note that, on step 3 (``tcg_gen_exit_tb()``), in addition to the
jump slot index, the address of the TB just executed is also returned.
This is important because that's the TB that will have to be patched
by the execution loop, and not necessarily the one that was directly
executed from it. This is due to the fact that the original TB might
have already been chained to additional TBs, which ended up being
executed without the execution loop's knowledge.

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

