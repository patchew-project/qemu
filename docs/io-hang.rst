========
I/O hang
========

Introduction
============

I/O hang is a mechanism to automatically rehandle AIOs when an error occurs on
the backend block device, which is unperceivable for Guest. If the error on the
backend device is temporary, like NFS returns EIO due to network fluctuations,
once the device is recovered, the AIOs will be resent automatically and done
successfully. If the error on the device is unrecoverable, the failed AIOs will
be returned to Guest after rehandle timeout.

This mechanism can provide self-recovery and high availability to VM. From the
view of Guest, AIOs sent to the device are hung for a time and the finished, and
any other unrelated service in Guest can work as usual.

Implementation
==============

Each BlockBackend will have a list to store AIOs which need be rehandled and a
timer to monitor the timeout.

If I/O hang is enabled, all returned AIOs will be checked and the failed ones
will be inserted into BlockBackend's list. The timer will be started and the
AIOs in the list will be resent to the backend device. Once the result of AIOs
relevant to this backend device is success, the AIOs will be returned back to
Guest. Otherwise, those AIOs will be rehandled periodically until timeout.

I/O hang provides interfaces to drain all the AIOs in BlockBackend's list. There
are some situations to drain the rehandling AIOs, eg. resetting virtio-blk.

I/O hang also provides qapi events, which can be used for manager tools like
libvirt to monitor.

Examples
========

Enable I/O hang when launching QEMU::

      $ qemu-system-x86_64                                      \
          -machine pc,accel=kvm -smp 1 -m 2048                  \
          -drive file=shared.img,format=raw,cache=none,aio=native,iohang-timeout=60

