Managed start up options
========================

In system mode emulation, it's possible to create a VM in a paused
state using the ``-S`` command line option. In this state the machine
is completely initialized according to command line options and ready
to execute VM code but VCPU threads are not executing any code. The VM
state in this paused state depends on the way QEMU was started. It
could be in:

- initial state (after reset/power on state)
- with direct kernel loading, the initial state could be amended to execute
  code loaded by QEMU in the VM's RAM and with incoming migration
- with incoming migration, initial state will be amended with the migrated
  machine state after migration completes

This paused state is typically used by users to query machine state and/or
additionally configure the machine (by hotplugging devices) in runtime before
allowing VM code to run.

However, at the ``-S`` pause point, it's impossible to configure options
that affect initial VM creation (like: ``-smp``/``-m``/``-numa`` ...) or
cold plug devices. The experimental ``--preconfig`` command line option
allows pausing QEMU before the initial VM creation, in a "preconfig" state,
where additional queries and configuration can be performed via QMP
before moving on to the resulting configuration startup. In the
preconfig state, QEMU only allows a limited set of commands over the
QMP monitor, where the commands do not depend on an initialized
machine, including but not limited to:

- ``qmp_capabilities``
- ``query-qmp-schema``
- ``query-commands``
- ``query-status``
- ``x-machine-init``
- ``x-exit-preconfig``

In particular these commands allow to advance and stop qemu at different
phases of the VM creation and finally to leave the "preconfig" state. The
accessible phases are:

- ``accel-created``
- ``initialized``
- ``ready``

The order of the phases is enforced. It is not possible to go backwards.
Note that other early phases exist, but they are not attainable with
``--preconfig``. Depending on the phase, QMP commands can be issued to modify
some part of the VM creation.

accel-created phase
-------------------

Initial phase entered with ``--preconfig``.

initialized phase
-----------------

``x-machine-init`` advances to ``initialized`` phase. During this phase, the
machine is initialized and populated with buses and devices. The following QMP
commands are available to manually populate or modify the machine:

- ``device_add``
- ``x-sysbus-mmio-map``
- ``qom-set``

ready phase
-----------

``x-exit-preconfig`` advances to the final phase. When entering this phase,
the VM creation finishes. "preconfig" state is then done and QEMU goes to
normal execution.

Machine creation example
------------------------

The following is an example that shows how to add some devices with qmp
commands, memory map them, and add interrupts::

  x-machine-init

  device_add        driver=sysbus-memory id=rom size=0x4000 readonly=true
  x-sysbus-mmio-map device=rom addr=32768

  device_add        driver=sysbus-memory id=flash size=0x80000 readonly=true
  x-sysbus-mmio-map device=flash addr=536870912

  device_add        driver=sysbus-memory id=ram size=0x10000
  x-sysbus-mmio-map device=ram addr=268435456

  device_add        driver=ibex-plic id=plic
  x-sysbus-mmio-map device=plic addr=1090584576

  device_add        driver=ibex-uart id=uart chardev=serial0
  x-sysbus-mmio-map device=uart addr=1073741824
  qom-set path=uart property=sysbus-irq[0] value=plic/unnamed-gpio-in[1]
  qom-set path=uart property=sysbus-irq[1] value=plic/unnamed-gpio-in[2]
  qom-set path=uart property=sysbus-irq[2] value=plic/unnamed-gpio-in[3]
  qom-set path=uart property=sysbus-irq[3] value=plic/unnamed-gpio-in[4]

  x-exit-preconfig

These commands reproduce a subset of the riscv32 opentitan (hw/riscv/opentitan)
machine. We can start qemu using::

  qemu-sytem-riscv32 -preconfig -qmp unix:./qmp-sock,server \
  -machine none -cpu lowriscv-ibex -serial mon:stdio ...

Then we just have to issue the commands, for example using `qmp-shell`. If the
previous commands were in a file named `machine.qmp`, we could do::

  qmp-shell ./qmp-sock < machine.qmp
