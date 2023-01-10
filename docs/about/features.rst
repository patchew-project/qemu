Features
========

Virtualisation
--------------

The most common use case for QEMU is to provide a virtual model of a
machine (CPU, memory and emulated devices) to run a guest OS. It
supports a number of hypervisors (known as accelerators) as well as a
dynamic JIT known as the Tiny Code Generator (TCG) capable of
emulating many CPUs.

.. list-table:: Supported Accelerators
  :header-rows: 1

  * - Accelerator
    - Host OS
    - Host Architectures
  * - KVM
    - Linux
    - Arm (64 bit only), MIPS, PPC, RISC-V, s390x, x86
  * - Xen
    - Linux (as dom0)
    - Arm, x86
  * - Intel HAXM (hax)
    - Linux, Windows
    - x86
  * - Hypervisor Framework (hvf)
    - MacOS
    - x86 (64 bit only), Arm (64 bit only)
  * - Windows Hypervisor Platform (wphx)
    - Windows
    - x86
  * - NetBSD Virtual Machine Monitor (nvmm)
    - NetBSD
    - x86
  * - Tiny Code Generator (tcg)
    - Linux, other POSIX, Windows, MacOS
    - Arm, x86, Loongarch64, MIPS, PPC, s390x, Sparc64, TCI [#tci]_

.. [#tci] The Tiny Code Interpreter (TCI) can be used where there is no
          explicit support for a processor backend. It will be even
          slower than normal TCG guests.

Related features
~~~~~~~~~~~~~~~~

System emulation provides a wide range of device models to emulate
various hardware components you may want to add to your machine. This
includes a wide number of VirtIO devices which are specifically tuned
for efficient operation under virtualisation. Some of the device
emulation can be offloaded from the main QEMU process using either
vhost-user (for VirtIO) or :ref:`Multi-process QEMU`. If the platform
supports it QEMU also supports directly passing devices through to
guest VMs to eliminate the device emulation overhead. See
:ref:`device-emulation` for more details.

There is a full featured block layer allows for construction of
complex storage typologies which can be stacked across multiple layers
supporting redirection, networking, snapshots and migration support.

The flexible ``chardev`` system allows for handling IO from character
like devices using stdio, files, unix sockets and TCP networking.

QEMU provides a number of management interfaces including a line based
Human Monitor Protocol (HMP) that allows you to dynamically add and
remove devices as well as introspect the system state. The QEMU
Monitor Protocol (QMP) is a well defined, versioned, machine usable
API that presents a rich interface to other tools to create, control
and manage Virtual Machines. This is the interface used by higher
level tools interfaces such as `Virt Manager
<https://virt-manager.org/>`_ using the `libvirt framework
<https://libvirt.org>`_. Using some sort of management layer to
configure complex QEMU setups is recommended.

For the common accelerators QEMU supported debugging with its
:ref:`gdbstub<GDB usage>` which allows users to connect GDB and debug
system software images.

See the :ref:`System Emulation` section of the manual for full details
of how to run QEMU as a VMM.

Emulation
---------

As alluded to above QEMU's Tiny Code Generator (TCG) also has the
ability to emulate a number of CPU architectures on any supported
platform. This can either be using full system emulation or using its
"user mode emulation" support to run user space processes compiled for
one CPU on another CPU.

See `User Mode Emulation` for more details on running in this mode.

.. list-table:: Supported Guest Architectures for Emulation
  :widths: 30 10 10 50
  :header-rows: 1

  * - Architecture (qemu name)
    - System
    - User-mode
    - Notes
  * - Alpha
    - Yes
    - Yes
    - Legacy 64 bit RISC ISA developed by DEC
  * - Arm (arm, aarch64)
    - Yes
    - Yes
    - Wide range of features, see :ref:`Arm Emulation` for details
  * - AVR
    - Yes
    - No
    - 8 bit micro controller, often used in maker projects
  * - Cris
    - Yes
    - Yes
    - Embedded RISC chip developed by AXIS
  * - Hexagon
    - No
    - Yes
    - Family of DSPs by Qualcomm
  * - PA-RISC (hppa)
    - Yes
    - Yes
    - A legacy RISC system used in HPs old minicomputers
  * - x86 (i386, x86_64)
    - Yes
    - Yes
    - The ubiquitous desktop PC CPU architecture, 32 and 64 bit.
  * - Loongarch
    - Yes
    - Yes
    - A MIPs-like 64bit RISC architecture developed in China
  * - m68k
    - Yes
    - Yes
    - Motorola 68000 variants and ColdFire
  * - Microblaze
    - Yes
    - Yes
    - RISC based soft-core by Xilinx
  * - MIPS (mips, mipsel, mips64, mips64el)
    - Yes
    - Yes
    - Venerable RISC architecture originally out of Stanford University
  * - Nios2
    - Yes
    - Yes
    - 32 bit embedded soft-core by Altera
  * - OpenRISC
    - Yes
    - Yes
    - Open source RISC architecture developed by the OpenRISC community
  * - Power (ppc, ppc64)
    - Yes
    - Yes
    - A general purpose RISC architecture now managed by IBM
  * - RISC-V
    - Yes
    - Yes
    - An open standard RISC ISA maintained by RISC-V International
  * - RX
    - Yes
    - No
    - A 32 bit micro controller developed by Renesas
  * - s390x
    - Yes
    - Yes
    - A 64 bit CPU found in IBM's System Z mainframes
  * - sh4
    - Yes
    - Yes
    - A 32 bit RISC embedded CPU developed by Hitachi
  * - SPARC (sparc, sparc64)
    - Yes
    - Yes
    - A RISC ISA originally developed by Sun Microsystems
  * - Tricore
    - Yes
    - No
    - A 32 bit RISC/uController/DSP developed by Infineon
  * - Xtensa
    - Yes
    - Yes
    - A configurable 32 bit soft core now owned by Cadence

Semihosting
~~~~~~~~~~~~

A number of guest architecture support :ref:`Semihosting` which
provides a way for guest programs to access the host system though a
POSIX-like system call layer. This has applications for early software
bring-up making it easy for a guest to dump data or read configuration
files before a full operating system is implemented.

Some of those guest architectures also support semihosting in
user-mode making the testing of "bare-metal" micro-controller code
easy in a user-mode environment that doesn't have a full libc port.

Deterministic Execution with Record/Replay
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For system emulation QEMU offers a execution mode called ``icount``
which allows for guest time to be purely a function of the number of
instructions executed. Combined with snapshots and a logging of HW
events a deterministic execution can be recorded and played back at
will.

gdbstub
~~~~~~~

Under emulation the :ref:`gdbstub<GDB usage>` is fully supported and
takes advantage of the implementation to support unlimited breakpoints
in the guest code. For system emulation we also support an unlimited
number of memory based watchpoints as well as integration with
record/replay to support reverse debugging.


TCG Plugins
~~~~~~~~~~~

In any emulation execution mode you can write :ref:`TCG Plugins` which
can instrument the guest code as it executes to a per-instruction
granularity. This is useful for writing tools to analyse the real
world execution behaviour of your programs.

Tools
-----

QEMU also provides a number of standalone commandline utilities, such
as the ``qemu-img`` disk image utility that allows you to create,
convert and modify disk images. While most are expected to be used in
conjunction with QEMU itself some can also be used with other VMMs
that support the same interfaces.

See :ref:`Tools` for more details.
