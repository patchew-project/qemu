.. SPDX-License-Identifier: GPL-2.0-or-later

========================
Laptop Lid Button Device
========================

The button device provides laptop lid button state information to the guest via
ACPI. Lid state is controlled via QMP commands, providing deterministic control
for testing and migration safety.

Configuration
-------------

The lid button device is created as an ISA device using ``-device button``.

Properties
~~~~~~~~~~

``ioport`` (default: 0x53d)
  I/O port base address for the lid button device register.

ACPI Interface
--------------

The lid button device is exposed to the guest as an ACPI device with:

* **HID**: ``PNP0C0D`` (Lid Device)
* **Device Path**: ``\_SB.LID0``
* **Notification Values**:

  * ``0x80``: Status change (lid opened/closed)

ACPI Methods
~~~~~~~~~~~~

``_LID`` (Lid Status)
  Returns the current lid state (0 = closed, 1 = open).

I/O Interface
-------------

The device uses a single I/O port register:

* **Port**: ``ioport`` property value (default 0x53d)
* **Size**: 1 byte
* **Access**: Read-only

Register Layout
~~~~~~~~~~~~~~~

**LIDS** (offset 0x00, 1 byte)
  Current lid state:

  * ``0x00``: Lid closed
  * ``0x01``: Lid open

QMP Commands
------------

The following QMP commands control the lid button state:

``lid-button-set-state``
  Set the lid button state.

  * ``open``: Whether the lid is open (boolean)

  Example::

    -> { "execute": "lid-button-set-state",
         "arguments": { "open": true }}
    <- { "return": {} }

``query-lid-button``
  Query the current lid button state.

  Example::

    -> { "execute": "query-lid-button" }
    <- { "return": { "open": true }}

Examples
--------

Basic usage::

  # Start VM with lid button
  qemu-system-x86_64 -device button -qmp tcp:localhost:4444,server,wait=off

  # From another terminal, set lid state via QMP:
  echo '{"execute":"qmp_capabilities"}
        {"execute":"lid-button-set-state",
         "arguments":{"open":false}}' | \
  nc -N localhost 4444

Simulate closing lid::

  # Start with lid open (default)
  # Close the lid
  echo '{"execute":"lid-button-set-state",
         "arguments":{"open":false}}' | nc -N localhost 4444

  # Later, open the lid
  echo '{"execute":"lid-button-set-state",
         "arguments":{"open":true}}' | nc -N localhost 4444

Test suspend on lid close::

  # Start VM with ACPI support
  qemu-system-x86_64 -device button

  # Close lid - guest OS should detect this and may suspend
  echo '{"execute":"lid-button-set-state",
         "arguments":{"open":false}}' | nc -N localhost 4444

  # Open lid - guest OS should wake or detect lid open
  echo '{"execute":"lid-button-set-state",
         "arguments":{"open":true}}' | nc -N localhost 4444

Combined with other laptop devices::

  # Create a complete laptop environment
  qemu-system-x86_64 -device battery -device acad -device button

  # Simulate closing lid while on battery power
  echo '{"execute":"battery-set-state",
         "arguments":{"state":{"present":true,"charging":false,
                               "discharging":true,"charge-percent":60}}}
        {"execute":"ac-adapter-set-state",
         "arguments":{"connected":false}}
        {"execute":"lid-button-set-state",
         "arguments":{"open":false}}' | nc -N localhost 4444
