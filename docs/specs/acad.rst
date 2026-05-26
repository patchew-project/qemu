.. SPDX-License-Identifier: GPL-2.0-or-later

=================
AC Adapter Device
=================

The AC adapter device provides AC power state information to the guest via ACPI.
AC adapter state is controlled via QMP commands, providing deterministic control
for testing and migration safety.

Configuration
-------------

The AC adapter device is created as an ISA device using ``-device acad``.

Properties
~~~~~~~~~~

``ioport`` (default: 0x53c)
  I/O port base address for the AC adapter device register.

ACPI Interface
--------------

The AC adapter device is exposed to the guest as an ACPI device with:

* **HID**: ``ACPI0003`` (AC Adapter)
* **Device Path**: ``\_SB.ADP0``
* **Notification Values**:

  * ``0x80``: Status change (connected/disconnected)

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

* **Port**: ``ioport`` property value (default 0x53c)
* **Size**: 1 byte
* **Access**: Read-only

Register Layout
~~~~~~~~~~~~~~~

**PWRS** (offset 0x00, 1 byte)
  Current AC adapter state:

  * ``0x00``: AC adapter offline (unplugged)
  * ``0x01``: AC adapter online (plugged in)

QMP Commands
------------

The following QMP commands control the AC adapter state:

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

Basic usage::

  # Start VM with AC adapter
  qemu-system-x86_64 -device acad -qmp tcp:localhost:4444,server,wait=off

  # From another terminal, set AC adapter state via QMP:
  echo '{"execute":"qmp_capabilities"}
        {"execute":"ac-adapter-set-state",
         "arguments":{"connected":true}}' | \
  nc -N localhost 4444

Simulate unplugging AC adapter::

  # Start with AC adapter connected
  echo '{"execute":"ac-adapter-set-state",
         "arguments":{"connected":true}}' | nc -N localhost 4444

  # Later, disconnect AC adapter
  echo '{"execute":"ac-adapter-set-state",
         "arguments":{"connected":false}}' | nc -N localhost 4444

  # Reconnect AC adapter
  echo '{"execute":"ac-adapter-set-state",
         "arguments":{"connected":true}}' | nc -N localhost 4444

Combined with battery device::

  # Create a complete laptop power environment
  qemu-system-x86_64 -device battery -device acad

  # Simulate unplugging AC while on battery
  echo '{"execute":"battery-set-state",
         "arguments":{"state":{"present":true,"charging":false,
                               "discharging":true,"charge-percent":75}}}
        {"execute":"ac-adapter-set-state",
         "arguments":{"connected":false}}' | nc -N localhost 4444
