=======================
QEMU laptop mirror tool
=======================

Synopsis
--------

**laptop-mirror.py** [*OPTIONS*]

Description
-----------

``laptop-mirror.py`` polls the host's battery, AC adapter and lid state
from sysfs/procfs and forwards every change to a running QEMU guest using
the ``battery-set-state``, ``ac-adapter-set-state`` and
``lid-button-set-state`` QMP commands. This script is a reference for how
a management layer (libvirt or similar) can wire host hardware to them,
and isn't meant for production use as-is.

Options
-------

.. program:: laptop-mirror.py

.. option:: -s SOCKET, --socket SOCKET

  QMP socket: a Unix path or ``host:port``. Falls back to ``$QMP_SOCKET``.

.. option:: -i SECONDS, --interval SECONDS

  Polling interval, in seconds. Default ``2.0``.

.. option:: --battery, --no-battery

  Mirror the battery (default: on).

.. option:: --ac-adapter, --no-ac-adapter

  Mirror the AC adapter (default: on).

.. option:: --lid, --no-lid

  Mirror the lid button (default: on). A device that is enabled but not
  present on the host is silently skipped.

.. option:: -v, --verbose

  ``-v`` logs every state change; ``-vv`` adds debug output.

Example
-------

Start QEMU with the laptop devices and a QMP socket::

  qemu-system-x86_64 \
      -device battery -device acad -device button \
      -qmp unix:/tmp/qmp.sock,server=on,wait=off \
      ...

Then mirror your host state::

  export QMP_SOCKET=/tmp/qmp.sock
  $builddir/run scripts/laptop-mirror.py -v

The script depends on the in-tree ``qemu.qmp`` package; ``$builddir/run``
puts it on ``PYTHONPATH``.

Caveats
-------

* QMP allows one client at a time. If ``qmp-shell``, libvirt or another
  script is already connected, the mirror times out after ten seconds and
  exits with an error.
* When QEMU runs as root, its Unix QMP socket is root-owned. Run the
  mirror as root too, ``chmod`` the socket after QEMU is up, or expose
  QMP over TCP.

See also
--------

:doc:`/specs/battery`, :doc:`/specs/acad`, :doc:`/specs/button`,
:manpage:`qemu-qmp-ref(7)`
