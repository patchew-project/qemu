QEMU Storage Daemon
===================

Synopsis
--------

**qemu-storage-daemon** [options]

Description
-----------

qemu-storage-daemon provides disk image functionality from QEMU, qemu-img, and
qemu-nbd in a long-running process controlled via QMP commands without running
a virtual machine. It can export disk images over NBD, run block job
operations, and perform other disk-related operations. The daemon is controlled
via a QMP monitor socket and initial configuration from the command-line.

The daemon offers the following subset of QEMU features:

* Blockdev nodes
* Block jobs
* NBD server
* Character devices
* Crypto and secrets
* QMP

Commands can be sent over a QEMU Monitor Protocol (QMP) connection. See the
:manpage:`qemu-storage-daemon-qmp-ref(7)` manual page for a description of the
commands.

The daemon runs until it is stopped using the ``quit`` QMP command or
SIGINT/SIGHUP/SIGTERM.

**Warning:** Never modify images in use by a running virtual machine or any
other process; this may destroy the image. Also, be aware that querying an
image that is being modified by another process may encounter inconsistent
state.

Options
-------

.. program:: qemu-storage-daemon

Standard options:

.. option:: -h, --help

  Display this help and exit

.. option:: -V, --version

  Display version information and exit

.. option:: -T, --trace [[enable=]PATTERN][,events=FILE][,file=FILE]

  .. include:: ../qemu-option-trace.rst.inc

.. option:: --blockdev BLOCKDEVDEF

  is a blockdev node definition. See the :manpage:`qemu(1)` manual page for a
  description of blockdev node properties and the
  :manpage:`qemu-block-drivers(7)` manual page for a description of
  driver-specific parameters.

.. option:: --chardev CHARDEVDEF

  is a character device definition. See the :manpage:`qemu(1)` manual page for
  a description of character device properties. A common character device
  definition configures a UNIX domain socket::

  --chardev socket,id=char1,path=/tmp/qmp.sock,server,nowait

.. option:: --monitor MONITORDEF

  is a QMP monitor definition. See the :manpage:`qemu(1)` manual page for
  a description of QMP monitor properties. A common QMP monitor definition
  configures a monitor on character device ``char1``::

  --monitor chardev=char1

.. option:: --nbd-server addr.type=inet,addr.host=<host>,addr.port=<port>[,tls-creds=<id>][,tls-authz=<id>]
  --nbd-server addr.type=unix,addr.path=<path>[,tls-creds=<id>][,tls-authz=<id>]

  is a NBD server definition. Both TCP and UNIX domain sockets are supported.
  TLS encryption can be configured using ``--object`` tls-creds-* and authz-*
  secrets (see below).

  To configure an NBD server on UNIX domain socket path ``/tmp/nbd.sock``::

  --nbd-server addr.type=unix,addr.path=/tmp/nbd.sock

.. option:: --object help
  --object <type>,help
  --object <type>[,<property>=<value>...]

  is a QEMU user creatable object definition. List object types with ``help``.
  List object properties with ``<type>,help``. See the :manpage:`qemu(1)`
  manual page for a description of the object properties. The most common
  object type is a ``secret``, which is used to supply passwords and/or
  encryption keys.

See also
--------

:manpage:`qemu(1)`, :manpage:`qemu-block-drivers(7)`, :manpage:`qemu-storage-daemon-qmp-ref(7)`
