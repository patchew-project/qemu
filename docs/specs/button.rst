.. SPDX-License-Identifier: GPL-2.0-or-later

========================
Laptop Lid Button Device
========================

The button device provides laptop lid button state information to the guest.
It supports two operating modes:

1. **QMP Control Mode** (default): Lid state is controlled via QMP commands,
   providing deterministic control for testing and migration safety.
2. **Procfs Mode**: Lid state mirrors the host's physical lid button, useful
   for desktop virtualization where the guest should see the host's lid state.

Configuration
-------------

The lid button device is created as an ISA device using ``-device button``.

Operating Modes
~~~~~~~~~~~~~~~

**QMP Control Mode** (``use-qmp=true``, default)
  Lid state is controlled via QMP commands. This mode is recommended for:

  * Production environments requiring migration support
  * Testing with predictable lid states
  * Environments without host lid button access
  * Security-sensitive deployments

**Procfs Mode** (``enable-procfs=true``)
  Lid mirrors the host's physical lid button. This mode is useful for:

  * Desktop virtualization on laptops
  * Development and testing with real lid button behavior

  Note: Procfs mode reads host files and runs timers, which may impact
  security and migration. Use with caution in production.

Properties
~~~~~~~~~~

``ioport`` (default: 0x53d)
  I/O port base address for the lid button device register.

``use-qmp`` (default: true)
  Enable QMP control mode. When true, lid state is controlled via
  QMP commands. Cannot be used together with ``enable-procfs=true``.

``enable-procfs`` (default: false)
  Enable procfs mode to mirror the host's lid button. Cannot be used together
  with ``use-qmp=true``.

``probe_interval`` (default: 2000)
  Time interval between periodic probes in milliseconds (procfs mode only).
  The minimum allowed value is 10ms to prevent excessive polling.

``procfs_path`` (default: /proc/acpi/button)
  Path to the host's lid button procfs directory (procfs mode only). The device
  will automatically scan this directory to find the lid state file. Use this
  property to specify a different path or to provide a custom location for
  testing purposes.

Host Lid Button Detection
-------------------------

The host's lid button information is taken from::

    /proc/acpi/button/lid/*/state

This file is expected to be formatted as:

- ``state:      open`` (if the lid is open)
- ``state:      closed`` (if the lid is closed)

These formats are based on the Linux 'button' driver.

The device automatically scans the ``/proc/acpi/button/lid/`` directory
for subdirectories containing a readable ``state`` file. If the procfs path
differs, a different lid button needs to be probed, or even if a "fake" host
lid button is to be provided, the ``procfs_path`` property allows overriding
the default detection.

ACPI Interface
--------------

The lid button device is exposed to the guest as an ACPI device with:

- **HID**: ``PNP0C0D`` (Lid Device)
- **Device Path**: ``\_SB.LID0``
- **Notification Values**:

  - ``0x80``: Status change (lid opened/closed)

ACPI Methods
~~~~~~~~~~~~

``_LID`` (Lid Status)
  Returns the current lid state (0 = closed, 1 = open).

I/O Interface
-------------

The device uses a single I/O port register:

- **Port**: ``ioport`` property value (default 0x53d)
- **Size**: 1 byte
- **Access**: Read-only

Register Layout
~~~~~~~~~~~~~~~

**LIDS** (offset 0x00, 1 byte)
  Current lid state:

  - ``0x00``: Lid closed
  - ``0x01``: Lid open

QMP Commands
------------

When using QMP control mode (default), the following commands are available:

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

QMP control mode (default - recommended)::

  # Start with QMP control
  qemu-system-x86_64 -device button -qmp tcp:localhost:4444,server,wait=off

  # From another terminal, set lid state via QMP:
  echo '{"execute":"qmp_capabilities"}
        {"execute":"lid-button-set-state",
         "arguments":{"open":false}}' | \
  nc -N localhost 4444

Procfs mode (mirror host lid button)::

  # Enable procfs mode to mirror host lid button
  qemu-system-x86_64 -device button,use-qmp=false,enable-procfs=true

  # Custom probe interval (5 seconds)
  qemu-system-x86_64 -device button,use-qmp=false,enable-procfs=true,probe_interval=5000

  # Custom procfs path
  qemu-system-x86_64 -device button,use-qmp=false,enable-procfs=true,procfs_path=/custom/path

Testing with fake lid button::

  # Create fake lid button files for testing
  mkdir -p /tmp/fake_lid/lid/LID0
  echo "state:      open" > /tmp/fake_lid/lid/LID0/state    # Format: "state:      open" or "state:      closed"

  # Use fake lid button in procfs mode
  qemu-system-x86_64 -device button,use-qmp=false,enable-procfs=true,procfs_path=/tmp/fake_lid

  # Update lid state while VM is running (from another terminal)
  echo "state:      closed" > /tmp/fake_lid/lid/LID0/state  # Close lid
  echo "state:      open" > /tmp/fake_lid/lid/LID0/state    # Open lid

Combined with other laptop devices::

  # QMP mode (recommended)
  qemu-system-x86_64 -device battery -device acad -device button

  # Procfs/sysfs mode (desktop virtualization)
  qemu-system-x86_64 -device battery,use-qmp=false,enable-sysfs=true \
                     -device acad,use-qmp=false,enable-sysfs=true \
                     -device button,use-qmp=false,enable-procfs=true
