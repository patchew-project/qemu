Multi-process QEMU
==================

This document describes how to configure and use multi-process qemu.
For the design document refer to docs/devel/qemu-multiprocess.

1) Configuration
----------------

To enable support for multi-process add --enable-mpqemu
to the list of options for the "configure" script.


2) Usage
--------

Multi-process QEMU requires an orchestrator to launch. Please refer to a
light-weight python based orchestrator for mpqemu in
scripts/mpqemu-launcher.py to lauch QEMU in multi-process mode.

scripts/mpqemu-launcher-perf-mode.py launches in "perf" mode. In this mode,
the same QEMU process connects to multiple remote devices, each emulated in
a separate process.

As of now, we only support the emulation of lsi53c895a in a separate process.

Following is a description of command-line used to launch mpqemu.

* Orchestrator:

  - The Orchestrator creates a unix socketpair

  - It launches the remote process and passes one of the
    sockets to it via command-line.

  - It then launches QEMU and specifies the other socket as an option
    to the Proxy device object

* Remote Process:

  - The first command-line option in the remote process is one of the
    sockets created by the Orchestrator

  - The remaining options are no different from how one launches QEMU with
    devices. The only other requirement is each PCI device must have a
    unique ID specified to it. This is needed to pair remote device with the
    Proxy object.

  - Example command-line for the remote process is as follows:

      /usr/bin/qemu-scsu-dev 4                                           \
      -device lsi53c895a,id=lsi0                                         \
      -drive id=drive_image2,file=/build/ol7-nvme-test-1.qcow2           \
      -device scsi-hd,id=drive2,drive=drive_image2,bus=lsi0.0,scsi-id=0

* QEMU:

  - Since parts of the RAM are shared between QEMU & remote process, a
    memory-backend-memfd is required to facilitate this, as follows:

    -object memory-backend-memfd,id=mem,size=2G

  - A "pci-proxy-dev" device is created for each of the PCI devices emulated
    in the remote process. A "socket" sub-option specifies the other end of
    unix channel created by orchestrator. The "id" sub-option must be specified
    and should be the same as the "id" specified for the remote PCI device

  - Example commandline for QEMU is as follows:

      -device pci-proxy-dev,id=lsi0,socket=3

* Monitor / QMP:

  - The remote process supports QEMU monitor. It could be specified using the
    "-monitor" or "-qmp" command-line options

  - As an example, one could connect to the monitor by adding the following
    to the command-line of the remote process

      -monitor unix:/home/qmp-sock,server,nowait

  - The user could connect to the monitor using the qmp script or using
    "socat" as outlined below:

      socat /home/qmp-sock stdio
