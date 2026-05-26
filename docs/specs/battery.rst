.. SPDX-License-Identifier: GPL-2.0-or-later

==============
Battery Device
==============

The battery device provides battery state information to the guest via ACPI.
Battery state is controlled via QMP commands, providing deterministic control
for testing and migration safety.

Configuration
-------------

The battery device is created as an ISA device using ``-device battery``.

Properties
~~~~~~~~~~

``ioport`` (default: 0x530)
  I/O port base address for the battery device registers.

ACPI Interface
--------------

The battery device exposes itself as an ACPI battery device with:

* **_HID**: ``PNP0C0A`` (Control Method Battery)
* **Device path**: ``\_SB.BAT0``

The device implements standard ACPI battery methods:

``_STA`` (Status)
  Returns the battery presence status.

``_BIF`` (Battery Information)
  Returns static battery information including design capacity,
  technology, and model information.

``_BST`` (Battery Status)
  Returns dynamic battery status including current state
  (charging/discharging), present rate, and remaining capacity.

I/O Interface
-------------

The battery device exposes 12 bytes of I/O space at the configured
I/O port address with the following layout:

* **Bytes 0-3**: Battery state (DWORD)

  Bits 0-3 hold the ACPI ``_BST`` state (1 = discharging,
  2 = charging); bit 4 carries the battery presence flag exposed by
  ``_STA`` (set when the battery is present).

* **Bytes 4-7**: Battery rate (DWORD)

  Current charge/discharge rate normalized to design capacity.

* **Bytes 8-11**: Battery charge (DWORD)

  Current battery charge level normalized to design capacity.

All values are normalized where the design capacity equals 10000 units.

ACPI Notifications
------------------

The battery device generates ACPI notifications through GPE events:

* **GPE._E08**: Status Change (0x80) - Battery state change

QMP Commands
------------

The following QMP commands control the battery state:

``battery-set-state``
  Set the battery state. Parameters:

  * ``present``: Whether the battery is present (boolean)
  * ``charging``: Whether the battery is charging (boolean)
  * ``discharging``: Whether the battery is discharging (boolean)
  * ``charge-percent``: Battery charge percentage 0-100 (integer)
  * ``rate``: Charge/discharge rate in mW (optional integer)

  Example::

    -> { "execute": "battery-set-state",
         "arguments": { "state": {
           "present": true,
           "charging": true,
           "discharging": false,
           "charge-percent": 85,
           "rate": 500
         }}}
    <- { "return": {} }

``query-battery``
  Query the current battery state. Returns the same fields as above.

  Example::

    -> { "execute": "query-battery" }
    <- { "return": {
           "present": true,
           "charging": true,
           "discharging": false,
           "charge-percent": 85,
           "rate": 500,
           "design-capacity": 10000
         }}

Examples
--------

Basic usage::

  # Start VM with battery device
  qemu-system-x86_64 -device battery -qmp tcp:localhost:4444,server,wait=off

  # From another terminal, set battery state via QMP:
  echo '{"execute":"qmp_capabilities"}
        {"execute":"battery-set-state",
         "arguments":{"state":{"present":true,"charging":false,
                               "discharging":true,"charge-percent":42,
                               "rate":500}}}' | \
  nc -N localhost 4444

Simulate battery discharge::

  # Start with 100% charged battery
  echo '{"execute":"battery-set-state",
         "arguments":{"state":{"present":true,"charging":false,
                               "discharging":true,"charge-percent":100,
                               "rate":1000}}}' | nc -N localhost 4444

  # Later, update to 50% charged
  echo '{"execute":"battery-set-state",
         "arguments":{"state":{"present":true,"charging":false,
                               "discharging":true,"charge-percent":50,
                               "rate":1000}}}' | nc -N localhost 4444

Simulate charging::

  # Start with 25% battery, begin charging
  echo '{"execute":"battery-set-state",
         "arguments":{"state":{"present":true,"charging":true,
                               "discharging":false,"charge-percent":25,
                               "rate":500}}}' | nc -N localhost 4444

Combined with other laptop devices::

  # Create a complete laptop environment
  qemu-system-x86_64 -device battery -device acad -device button
