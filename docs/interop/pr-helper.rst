..

======================================
Persistent reservation helper protocol
======================================

QEMU's SCSI passthrough devices, ``scsi-block`` and ``scsi-generic``,
can delegate implementation of persistent reservations to an external
(and typically privilege) program.  Persistent Reservations allow
restricting access to block devices to specific initiators in a shared
storage setup.

For a more detailed reference please refer the the SCSI Primary
Commands standard, specifically the section on Reservations and the
"PERSISTENT RESERVE IN" and "PERSISTENT RESERVE OUT" commands.

This document describes the socket protocol used between QEMU's
``pr-manager-helper`` object and the external program.

.. contents::

Connection and initialization
-----------------------------

After connecting to the helper program's socket, the helper starts a simple
feature negotiation process by writing four bytes corresponding to
the features it exposes (``supported_features``).  QEMU also writes
four bytes corresponding to the desired features of the helper program
(``requested_features``).

If a bit is 1 in ``requested_features`` and 0 in ``supported_features``,
the corresponding feature is not supported by the helper and the connection
is closed.  On the other hand, it is acceptable for a bit to be 0 in
``requested_features`` and 1 in ``supported_features``; in this case,
he helper will not enable the feature.

Right now no feature is defined, so the two parts always write four
zero bytes.

All data transmitted on the socket is big-endian.

Command format
--------------

A command consists of a request and a response.  A request consists
of a 16-byte SCSI CDB.  A file descriptor must be passed to the helper
together with the SCSI CDB using ancillary data.

The CDB has the following limitations:

- the command (stored in the first byte) must be one of 0x5E
  (PERSISTENT RESERVE IN) or 0x5F (PERSISTENT RESERVE OUT).

- the allocation length (stored in bytes 7-8 of the CDB for PERSISTENT
  RESERVE IN) or parameter list length (stored in bytes 5-8 of the CDB
  for PERSISTENT RESERVE OUT) is limited to 8 KiB.

For PERSISTENT RESERVE OUT, the parameter list is sent right after the
CDB.

The helper's reply has the following fixed structure:

- 4 bytes for the SCSI status

- 96 bytes for the SCSI sense data

The sense data is always sent to keep the protocol simple, even though
it is only valid if the SCSI status is CHECK CONDITION (0x02).

For PERSISTENT RESERVE IN, the data is sent right after the sense
data if the SCSI status is GOOD (0x00).

It is invalid to send multiple commands concurrently on the same
socket.  It is however possible to connect multiple sockets to the
helper and send multiple commands to the helper for one or more
file descriptors.

If the protocol is violated, the helper closes the socket.
