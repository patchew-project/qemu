Native Library Calls Optimization
=================================

Description
-----------

Executing a program under QEMU's user mode subjects the entire
program, including all library calls, to translation. It's important
to understand that many of these library functions are optimized
specifically for the guest architecture. Therefore, their
translation might not yield the most efficient execution.

When the semantics of a library function are well defined, we can
capitalize on this by substituting the translated version with a call
to the native equivalent function.

To achieve tangible results, focus should be given to functions such
as memory-related ('mem*') and string-related ('str*') functions.
These subsets of functions often have the most significant effect
on overall performance, making them optimal candidates for
optimization.

Implementation
--------------

Upon setting the LD_PRELOAD environment variable, the dynamic linker
will load the library specified in LD_PRELOAD preferentially. If there
exist functions in the LD_PRELOAD library that share names with those
in other libraries, they will override the corresponding functions in
those other libraries.

To implement native library bypass, we created a shared library and
re-implemented the native functions within it as a special
instruction sequence. By means of the LD_PRELOAD environment
variable, we load this shared library into the user program.
Therefore, when the user program calls a native function, it actually
executes this special instruction sequence. During execution, QEMU's
translator captures these special instructions and executes the
corresponding native functions.

These special instructions are implemented using
architecture-specific unused or invalid opcodes, ensuring that they
do not conflict with existing instructions.


i386 and x86_64
---------------
An unused instruction is utilized to mark a native call.

arm and aarch64
---------------
HLT is an invalid instruction for userspace and usefully has 16
bits of spare immeadiate data which we can stuff data in.

mips and mips64
---------------
The syscall instruction contains 20 unused bits, which are typically
set to 0. These bits can be used to store non-zero data,
distinguishing them from a regular syscall instruction.

Usage
-----

1. Install cross-compilation tools

Cross-compilation tools are required to build the shared libraries
that can hook the necessary library functions. For example, a viable
command on Ubuntu is:

::

    apt install gcc-arm-linux-gnueabihf gcc-aarch64-linux-gnu \
    gcc-mips-linux-gnu gcc-mips64-linux-gnuabi64


2. Locate the compiled libnative.so

After compilation, the libnative.so file can be found in the
``./build/common-user/native/<target>-linux-user`` directory.

3. Run the program with the ``--native-bypass`` option

To run your program with native library bypass, use the
``--native-bypass`` option to import libnative.so:

::

    qemu-<target> --native-bypass \
    ./build/common-user/native/<target>-linux-user/libnative.so ./program

