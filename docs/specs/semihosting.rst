.. _Semihosting:

Semihosting
-----------

Semihosting is a feature provided by a number of guests that allow the
program running on the target to interact with the host system. On
real hardware this is usually provided by a debugger hooked directly
to the system.

Generally semihosting makes it easier to bring up low level code before a
more fully functional operating system has been enabled. On QEMU it
also allows for embedded micro-controller code which typically doesn't
have a full libc to be run as "bare-metal" code under QEMU's user-mode
emulation. It is also useful for writing test cases and indeed a
number of compiler suites as well as QEMU itself use semihosting calls
to exit test code while reporting the success state.

Semihosting is only available using TCG emulation. This is because the
instructions to trigger a semihosting call are typically reserved
causing most hypervisors to trap and fault on them.

.. warning::
   Semihosting inherently bypasses any isolation there may be between
   the guest and the host. As a result a program using semihosting can
   happily trash your host system. You should only ever run trusted
   code with semihosting enabled.

Redirection
~~~~~~~~~~~

Semihosting calls can be re-directed to a (potentially remote) gdb
during debugging via the :ref:`gdbstub<GDB usage>`. Output to the
semihosting console is configured as a ``chardev`` so can be
redirected to a file, pipe or socket like any other ``chardev``
device.

See :ref:`Semihosting Options<Semihosting Options>` for details.

Supported Targets
~~~~~~~~~~~~~~~~~

Most targets offer a similar semihosting implementations with some
minor changes to define the appropriate instruction to encode the
semihosting call and which registers hold the parameters. They tend to
presents a simple POSIX-like API which allows your program to read and
write files, access the console and some other basic interactions.

.. note::
   QEMU makes an implementation decision to implement all file access
   in ``O_BINARY`` mode regardless of the host operating system. This
   is because gdb semihosting support doesn't make the distinction
   between the modes and magically processing line endings can be confusing.

.. list-table:: Guest Architectures supporting Semihosting
  :widths: 10 10 80
  :header-rows: 1

  * - Architecture
    - Modes
    - Specification
  * - Arm
    - System and User-mode
    - https://github.com/ARM-software/abi-aa/blob/main/semihosting/semihosting.rst
  * - m68k
    - System
    - https://sourceware.org/git/?p=newlib-cygwin.git;a=blob;f=libgloss/m68k/m68k-semi.txt;hb=HEAD
  * - mips
    - System
    - Unified Hosting Interface (MD01069)
  * - Nios II
    - System
    - https://sourceware.org/git/gitweb.cgi?p=newlib-cygwin.git;a=blob;f=libgloss/nios2/nios2-semi.txt;hb=HEAD
  * - RISC-V
    - System and User-mode
    - https://github.com/riscv/riscv-semihosting-spec/blob/main/riscv-semihosting-spec.adoc
  * - Xtensa
    - System
    - Tensilica ISS SIMCALL
