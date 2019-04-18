==============
Security Guide
==============
Overview
--------
This guide covers security topics relevant to developers working on QEMU.  It
includes an explanation of the security requirements that QEMU gives its users,
the architecture of the code, and secure coding practices.

Security Requirements
---------------------
QEMU supports many different use cases, some of which have stricter security
requirements than others.  The community has agreed on the overall security
requirements that users may depend on.  These requirements define what is
considered supported from a security perspective.

Virtualization Use Case
~~~~~~~~~~~~~~~~~~~~~~~
The virtualization use case covers cloud and virtual private server (VPS)
hosting, as well as traditional data center and desktop virtualization.  These
use cases rely on hardware virtualization extensions to execute guest code
safely on the physical CPU at close-to-native speed.

The following entities are **untrusted**, meaning that they may be buggy or
malicious:

* Guest
* User-facing interfaces (e.g. VNC, SPICE, WebSocket)
* Network protocols (e.g. NBD, live migration)
* User-supplied files (e.g. disk images, kernels, device trees)

Bugs affecting these entities are evaluated on whether they can cause damage in
real-world use cases and treated as security bugs if this is the case.

Non-virtualization Use Case
~~~~~~~~~~~~~~~~~~~~~~~~~~~
The non-virtualization use case covers emulation using the Tiny Code Generator
(TCG).  In principle the TCG and device emulation code used in conjunction with
the non-virtualization use case should meet the same security requirements as
the virtualization use case.  However, for historical reasons much of the
non-virtualization use case code was not written with these security
requirements in mind.

Bugs affecting the non-virtualization use case are not considered security
bugs at this time.  Users with non-virtualization use cases must not rely on
QEMU to provide guest isolation or any security guarantees.

Architecture
------------
This section describes the design principles that ensure the security
requirements are met.

Guest Isolation
~~~~~~~~~~~~~~~
Guest isolation is the confinement of guest code to the virtual machine.  When
guest code gains control of execution on the host this is called escaping the
virtual machine.  Isolation also includes resource limits such as CPU, memory,
disk, or network throttling.  Guests must be unable to exceed their resource
limits.

QEMU presents an attack surface to the guest in the form of emulated devices.
The guest must not be able to gain control of QEMU.  Bugs in emulated devices
could allow malicious guests to gain code execution in QEMU.  At this point the
guest has escaped the virtual machine and is able to act in the context of the
QEMU process on the host.

Guests often interact with other guests and share resources with them.  A
malicious guest must not gain control of other guests or access their data.
Disk image files and network traffic must be protected from other guests unless
explicitly shared between them by the user.

Principle of Least Privilege
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The principle of least privilege states that each component only has access to
the privileges necessary for its function.  In the case of QEMU this means that
each process only has access to resources belonging to the guest.

The QEMU process should not have access to any resources that are inaccessible
to the guest.  This way the guest does not gain anything by escaping into the
QEMU process since it already has access to those same resources from within
the guest.

Following the principle of least privilege immediately fulfills guest isolation
requirements.  For example, guest A only has access to its own disk image file
``a.img`` and not guest B's disk image file ``b.img``.

In reality certain resources are inaccessible to the guest but must be
available to QEMU to perform its function.  For example, host system calls are
necessary for QEMU but are not exposed to guests.  A guest that escapes into
the QEMU process can then begin invoking host system calls.

New features must be designed to follow the principle of least privilege.
Should this not be possible for technical reasons, the security risk must be
clearly documented so users are aware of the trade-off of enabling the feature.

Isolation mechanisms
~~~~~~~~~~~~~~~~~~~~
Several isolation mechanisms are available to realize this architecture of
guest isolation and the principle of least privilege.  With the exception of
Linux seccomp, these mechanisms are all deployed by management tools that
launch QEMU, such as libvirt.  They are also platform-specific so they are only
described briefly for Linux here.

The fundamental isolation mechanism is that QEMU processes must run as
**unprivileged users**.  Sometimes it seems more convenient to launch QEMU as
root to give it access to host devices (e.g. ``/dev/net/tun``) but this poses a
huge security risk.  File descriptor passing can be used to give an otherwise
unprivileged QEMU process access to host devices without running QEMU as root.

**SELinux** and **AppArmor** make it possible to confine processes beyond the
traditional UNIX process and file permissions model.  They restrict the QEMU
process from accessing processes and files on the host system that are not
needed by QEMU.

**Resource limits** and **cgroup controllers** provide throughput and utilization
limits on key resources such as CPU time, memory, and I/O bandwidth.

**Linux namespaces** can be used to make process, file system, and other system
resources unavailable to QEMU.  A namespaced QEMU process is restricted to only
those resources that were granted to it.

**Linux seccomp** is available via the QEMU ``--sandbox`` option.  It disables
system calls that are not needed by QEMU, thereby reducing the host kernel
attack surface.

Secure coding practices
-----------------------
At the source code level there are several points to keep in mind.  Both
developers and security researchers must be aware of them so that they can
develop safe code and audit existing code properly.

General Secure C Coding Practices
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Most CVEs (security bugs) reported against QEMU are not specific to
virtualization or emulation.  They are simply C programming bugs.  Therefore
it's critical to be aware of common classes of security bugs.

There is a wide selection of resources available covering secure C coding.  For
example, the `CERT C Coding Standard
<https://wiki.sei.cmu.edu/confluence/display/c/SEI+CERT+C+Coding+Standard>`_
covers the most important classes of security bugs.

Instead of describing them in detail here, only the names of the most important
classes of security bugs are mentioned:

* Buffer overflows
* Use-after-free and double-free
* Integer overflows
* Format string vulnerabilities

Some of these classes of bugs can be detected by analyzers.  Static analysis is
performed regularly by Coverity and the most obvious of these bugs are even
reported by compilers.  Dynamic analysis is possible with valgrind, tsan, and
asan.

Input Validation
~~~~~~~~~~~~~~~~
Inputs from the guest or external sources (e.g. network, files) cannot be
trusted and may be invalid.  Inputs must be checked before using them in a way
that could crash the program, expose host memory to the guest, or otherwise be
exploitable by an attacker.

The most sensitive attack surface is device emulation.  All hardware register
accesses and data read from guest memory must be validated.  A typical example
is a device that contains multiple units that are selectable by the guest via
an index register::

  typedef struct {
      ProcessingUnit unit[2];
      ...
  } MyDeviceState;

  static void mydev_writel(void *opaque, uint32_t addr, uint32_t val)
  {
      MyDeviceState *mydev = opaque;
      ProcessingUnit *unit;

      switch (addr) {
      case MYDEV_SELECT_UNIT:
          unit = &mydev->unit[val];   <-- this input wasn't validated!
          ...
      }
  }

If ``val`` is not in range [0, 1] then an out-of-bounds memory access will take
place when ``unit`` is dereferenced.  The code must check that ``val`` is 0 or
1 and handle the case where it is invalid.

Unexpected Device Accesses
~~~~~~~~~~~~~~~~~~~~~~~~~~
The guest may access device registers in unusual orders or at unexpected
moments.  Device emulation code must not assume that the guest follows the
typical "theory of operation" presented in driver writer manuals.  The guest
may make nonsense accesses to device registers such as starting operations
before the device has been fully initialized.

A related issue is that device emulation code must be prepared for unexpected
device register accesses while asynchronous operations are in progress.  A
well-behaved guest might wait for a completion interrupt before accessing
certain device registers.  Device emulation code must handle the case where the
guest overwrites registers or submits further requests before an ongoing
request completes.  Unexpected accesses must not cause memory corruption or
leaks in QEMU.

Live migration
~~~~~~~~~~~~~~
Device state can be saved to disk image files and shared with other users.
Live migration code must validate inputs when loading device state so an
attacker cannot gain control by crafting invalid device states.  Device state
is therefore considered untrusted even though it is typically generated by QEMU
itself.

Guest Memory Access Races
~~~~~~~~~~~~~~~~~~~~~~~~~
Guests with multiple vCPUs may modify guest RAM while device emulation code is
running.  Device emulation code must copy in descriptors and other guest RAM
structures and only process the local copy.  This prevents
time-of-check-to-time-of-use (TOCTOU) race conditions that could cause QEMU to
crash when a vCPU thread modifies guest RAM while device emulation is
processing it.
