.. SPDX-License-Identifier: GPL-2.0-or-later

=================
AC Adapter Device
=================

The AC adapter device provides AC power state information to the guest. It
supports two operating modes:

1. **QMP Control Mode** (default): AC adapter state is controlled via QMP
   commands, providing deterministic control for testing and migration safety.
2. **Sysfs Mode**: AC adapter state mirrors the host's physical AC adapter,
   useful for desktop virtualization where the guest should see the host's
   power state.

Configuration
-------------

The AC adapter device is created as an ISA device using ``-device acad``.

Operating Modes
~~~~~~~~~~~~~~~

**QMP Control Mode** (``use-qmp=true``, default)
  AC adapter state is controlled via QMP commands. This mode is recommended for:

  * Production environments requiring migration support
  * Testing with predictable power states
  * Environments without host AC adapter access
  * Security-sensitive deployments

**Sysfs Mode** (``enable-sysfs=true``)
  AC adapter mirrors the host's physical AC adapter. This mode is useful for:

  * Desktop virtualization on laptops
  * Development and testing with real AC adapter behavior

  Note: Sysfs mode reads host files and runs timers, which may impact
  security and migration. Use with caution in production.

Properties
~~~~~~~~~~

``ioport`` (default: 0x53c)
  I/O port base address for the AC adapter device register.

``use-qmp`` (default: true)
  Enable QMP control mode. When true, AC adapter state is controlled via
  QMP commands. Cannot be used together with ``enable-sysfs=true``.

``enable-sysfs`` (default: false)
  Enable sysfs mode to mirror the host's AC adapter. Cannot be used together
  with ``use-qmp=true``.

``probe_interval`` (default: 2000)
  Time interval between periodic probes in milliseconds (sysfs mode only).
  A zero value disables the periodic probes, and makes the AC adapter state
  updates occur on guest requests only.

``sysfs_path`` (default: auto-detected)
  Path to the host's AC adapter sysfs directory (sysfs mode only). By default,
  the device auto-detects the first AC adapter of type "Mains" in
  ``/sys/class/power_supply/``. Use this property to specify a different
  AC adapter, or to provide a custom path for testing purposes.

Host AC Adapter Detection
-------------------------

The host's AC adapter information is taken from the sysfs AC adapter
data, located in::

    /sys/class/power_supply/[device of type "Mains"]

The device automatically scans for the first AC adapter with:

- A ``type`` file containing "Mains"
- An ``online`` file that can be read

If the sysfs path differs, a different AC adapter needs to be probed,
or even if a "fake" host AC adapter is to be provided, the ``sysfs_path``
property allows overriding the default detection.

ACPI Interface
--------------

The AC adapter device is exposed to the guest as an ACPI device with:

- **HID**: ``ACPI0003`` (AC Adapter)
- **Device Path**: ``\_SB.ADP0``
- **Notification Values**:

  - ``0x80``: Status change (connected/disconnected)

ACPI Methods
~~~~~~~~~~~~

``_PSR`` (Power Source)
  Returns the current AC adapter state (0 = offline, 1 = online).

``_PCL`` (Power Consumer List)
  Returns the list of devices powered by this adapter.

``_PIF`` (Power Source Information)
  Returns static information about the power source including model number,
  serial number, and OEM information.

I/O Interface
-------------

The device uses a single I/O port register:

- **Port**: ``ioport`` property value (default 0x53c)
- **Size**: 1 byte
- **Access**: Read-only

Register Layout
~~~~~~~~~~~~~~~

**PWRS** (offset 0x00, 1 byte)
  Current AC adapter state:

  - ``0x00``: AC adapter offline (unplugged)
  - ``0x01``: AC adapter online (plugged in)

QMP Commands
------------

When using QMP control mode (default), the following commands are available:

``ac-adapter-set-state``
  Set the AC adapter connection state.

  * ``connected``: Whether the AC adapter is connected (boolean)

  Example::

    -> { "execute": "ac-adapter-set-state",
         "arguments": { "connected": true }}
    <- { "return": {} }

``query-ac-adapter``
  Query the current AC adapter state.

  Example::

    -> { "execute": "query-ac-adapter" }
    <- { "return": { "connected": true }}

Examples
--------

QMP control mode (default - recommended)::

  # Start with QMP control
  qemu-system-x86_64 -device acad -qmp tcp:localhost:4444,server,wait=off

  # From another terminal, set AC adapter state via QMP:
  echo '{"execute":"qmp_capabilities"}
        {"execute":"ac-adapter-set-state",
         "arguments":{"connected":true}}' | \
  nc -N localhost 4444

Sysfs mode (mirror host AC adapter)::

  # Enable sysfs mode to mirror host AC adapter
  qemu-system-x86_64 -device acad,use-qmp=false,enable-sysfs=true

  # Custom probe interval (5 seconds)
  qemu-system-x86_64 -device acad,use-qmp=false,enable-sysfs=true,probe_interval=5000

  # Specific AC adapter path
  qemu-system-x86_64 -device acad,use-qmp=false,enable-sysfs=true,sysfs_path=/sys/class/power_supply/ADP1

Testing with fake AC adapter::

  # Create fake AC adapter files for testing
  mkdir -p /tmp/fake_ac
  echo "Mains" > /tmp/fake_ac/type
  echo "1" > /tmp/fake_ac/online          # 1 = connected, 0 = disconnected

  # Use fake AC adapter in sysfs mode
  qemu-system-x86_64 -device acad,use-qmp=false,enable-sysfs=true,sysfs_path=/tmp/fake_ac

  # Update AC adapter state while VM is running (from another terminal)
  echo "0" > /tmp/fake_ac/online          # Disconnect AC adapter
  echo "1" > /tmp/fake_ac/online          # Reconnect AC adapter

Combined with battery device::

  # QMP mode (recommended)
  qemu-system-x86_64 -device battery -device acad

  # Sysfs mode (desktop virtualization)
  qemu-system-x86_64 -device battery,use-qmp=false,enable-sysfs=true \
                     -device acad,use-qmp=false,enable-sysfs=true
