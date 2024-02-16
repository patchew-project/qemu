Inter-VM Shared Memory Flat Device
----------------------------------

The ivshmem-flat device is meant to be used on machines that lack a PCI bus,
making them unsuitable for the use of the traditional ivshmem device modeled as
a PCI device. Machines like those with a Cortex-M MCU are good candidates to use
the ivshmem-flat device. Also, since the flat version maps the control and
status registers directly to the memory, it requires a quite tiny "device
driver" to interact with other VMs, which is useful in some RTOSes, like
Zephyr, which usually run on constrained resource targets.

Similar to the ivshmem device, the ivshmem-flat device supports both peer
notification via HW interrupts and Inter-VM shared memory. This allows the
device to be used together with the traditional ivshmem, enabling communication
between, for instance, an aarch64 VM  (using the traditional ivshmem device and
running Linux), and an arm VM (using the ivshmem-flat device and running Zephyr
instead).

The ivshmem-flat device does not support the use of a ``memdev`` option (see
ivshmem.rst for more details). It relies on the ivshmem server to create and
distribute the proper shared memory file descriptor and the eventfd(s) to notify
(interrupt) the peers. Therefore, to use this device, it is always necessary to
have an ivshmem server up and running for proper device creation.

Although the ivshmem-flat supports both peer notification (interrupts) and
shared memory, the interrupt mechanism is optional. If no input IRQ is
specified for the device it is disabled, preventing the VM from notifying or
being notified by other VMs (a warning will be displayed to the user to inform
the IRQ mechanism is disabled). The shared memory region is always present.

The MMRs (INTRMASK, INTRSTATUS, IVPOSITION, and DOORBELL registers) offsets at
the MMR region, and their functions, follow the ivshmem spec, so they work
exactly as in the ivshmem PCI device (see ./specs/ivshmem-spec.txt).


Device Options
--------------

The only required options to create an ivshmem-flat device are: (a) the UNIX
socket where the ivshmem server is listening, usually ``/tmp/ivshmem_socket``;
and (b) the bus type to be used by the device, which currently only supports
"/sysbus" bus type.

Example:

.. parsed-literal::

    |qemu-system-arm| -chardev socket,path=/tmp/ivshmem_socket,id=ivshmem_flat -device ivshmem-flat,x-bus-qompath="/sysbus",chardev=ivshmem_flat

The other options are for fine tuning the device.

``x-irq-qompath``. Used to inform the device which IRQ input line it can attach
to enable the notification mechanism (IRQ). The ivshmem-flat device currently
only supports notification via vector 0, ignoring other vectors.

Two examples for different machines follow.

Stellaris machine (``- machine lm3s6965evb``):

::

    x-irq-qompath=/machine/unattached/device[1]/nvic/unnamed-gpio-in[0]

Arm mps2-an385 machine (``-machine mps2-an385``):

::

    x-irq-qompath=/machine/armv7m/nvic/unnamed-gpio-in[0]

The available IRQ input lines on a given VM that the ivshmem-flat device can be
attached to can be inspected from the QEMU monitor (Ctrl-a + c) with:

(qemu) info qom-tree

``x-bus-address-mmr``. Allows changing the address where the MMRs are mapped
into the VM memory layout. Default is 0x400FF000, but this address might be
already taken on some VMs, hence it's  necessary to adjust the MMR location on
some VMs.

 ``x-bus-address-shmem``. Allows changing the address where the shared memory
region is mapped into the VM memory layout. Default is 0x40100000, but this
address might be already taken on some VMs, hence it's necessary to adjust the
shared memory location.

``shmem-size``. Allows changing the size (in bytes) of shared memroy region.
Default is 4 MiB, which is the same default value used by the ivshmem server, so
usually it's not necessary to change it. The  size must match the size of the
shared memory reserverd and informed by the ivshmem server, otherwise device
creation fails.
