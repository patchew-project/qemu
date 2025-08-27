.. SPDX-License-Identifier: GPL-2.0-or-later

==============
Battery Device
==============

The battery device provides battery state information to the guest. It supports
two operating modes:

1. **QMP Control Mode** (default): Battery state is controlled via QMP commands,
   providing deterministic control for testing and migration safety.
2. **Sysfs Mode**: Battery state mirrors the host's physical battery, useful
   for desktop virtualization where the guest should see the host's battery.

Configuration
-------------

The battery device is created as an ISA device using ``-device battery``.

Operating Modes
~~~~~~~~~~~~~~~

**QMP Control Mode** (``use-qmp=true``, default)
  Battery state is controlled via QMP commands. This mode is recommended for:

  * Production environments requiring migration support
  * Testing with predictable battery states
  * Environments without host battery access
  * Security-sensitive deployments

**Sysfs Mode** (``enable-sysfs=true``)
  Battery mirrors the host's physical battery. This mode is useful for:

  * Desktop virtualization on laptops
  * Development and testing with real battery behavior

  Note: Sysfs mode reads host files and runs timers, which may impact
  security and migration. Use with caution in production.

Properties
~~~~~~~~~~

``ioport`` (default: 0x530)
  I/O port base address for the battery device registers.

``use-qmp`` (default: true)
  Enable QMP control mode. When true, battery state is controlled via
  QMP commands. Cannot be used together with ``enable-sysfs=true``.

``enable-sysfs`` (default: false)
  Enable sysfs mode to mirror the host's battery. Cannot be used together
  with ``use-qmp=true``.

``probe_interval`` (default: 2000)
  Time interval between periodic probes in milliseconds (sysfs mode only).
  A zero value disables the periodic probes, and makes the battery state
  updates occur on guest requests only.

``sysfs_path`` (default: auto-detected)
  Path to the host's battery sysfs directory (sysfs mode only). If not
  specified, the device will automatically detect the battery from
  ``/sys/class/power_supply/``. This property allows overriding the default
  path if:

  * The sysfs path differs from the standard location
  * A different battery needs to be probed
  * A "fake" host battery is to be provided for testing

Host Battery Detection
----------------------

The host's battery information is taken from the sysfs battery data,
located in::

  /sys/class/power_supply/[device of type "Battery"]

The device automatically scans for the first available battery device
with type "Battery" in the power_supply directory.

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

  * 1 = Discharging
  * 2 = Charging

* **Bytes 4-7**: Battery rate (DWORD)

  Current charge/discharge rate normalized to design capacity.

* **Bytes 8-11**: Battery charge (DWORD)

  Current battery charge level normalized to design capacity.

All values are normalized where the design capacity equals 10000 units.
Unknown values are represented as 0xFFFFFFFF.

ACPI Notifications
------------------

The battery device generates ACPI notifications through GPE events:

* **GPE._E07**: Device Check (0x01) - Battery insertion/removal
* **GPE._E08**: Status Change (0x80) - Battery state change
* **GPE._E09**: Information Change (0x81) - Battery information update

QMP Commands
------------

When using QMP control mode (default), the following commands are available:

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

QMP control mode (default - recommended)::

  # Start with QMP control
  qemu-system-x86_64 -device battery -qmp tcp:localhost:4444,server,wait=off

  # From another terminal, set battery state via QMP:
  echo '{"execute":"qmp_capabilities"}
        {"execute":"battery-set-state",
         "arguments":{"state":{"present":true,"charging":false,
                               "discharging":true,"charge-percent":42,
                               "rate":500}}}' | \
  nc -N localhost 4444

Sysfs mode (mirror host battery)::

  # Enable sysfs mode to mirror host battery
  qemu-system-x86_64 -device battery,use-qmp=false,enable-sysfs=true

  # Custom probe interval (5 seconds)
  qemu-system-x86_64 -device battery,use-qmp=false,enable-sysfs=true,probe_interval=5000

  # Specific battery path
  qemu-system-x86_64 -device battery,use-qmp=false,enable-sysfs=true,sysfs_path=/sys/class/power_supply/BAT1

Testing with fake battery::

  # Create fake battery files for testing
  mkdir -p /tmp/fake_battery
  echo "Battery" > /tmp/fake_battery/type
  echo "Discharging" > /tmp/fake_battery/status
  echo "50" > /tmp/fake_battery/capacity
  echo "1500000" > /tmp/fake_battery/energy_now    # Current energy in μWh
  echo "3000000" > /tmp/fake_battery/energy_full   # Full capacity in μWh
  echo "500000" > /tmp/fake_battery/power_now      # Current power in μW

  # Use fake battery in sysfs mode
  qemu-system-x86_64 -device battery,use-qmp=false,enable-sysfs=true,sysfs_path=/tmp/fake_battery

  # Update battery state while VM is running (from another terminal)
  # Change to 75% charging:
  echo "Charging" > /tmp/fake_battery/status
  echo "75" > /tmp/fake_battery/capacity
  echo "2250000" > /tmp/fake_battery/energy_now    # 75% of 3000000
  echo "500000" > /tmp/fake_battery/power_now      # Charging rate (500 mW)

  # Change to 25% discharging:
  echo "Discharging" > /tmp/fake_battery/status
  echo "25" > /tmp/fake_battery/capacity
  echo "750000" > /tmp/fake_battery/energy_now     # 25% of 3000000
  echo "300000" > /tmp/fake_battery/power_now      # Discharge rate (300 mW)
