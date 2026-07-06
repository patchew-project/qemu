NXP i.MX 8M Plus and i.MX 8M Mini Evaluation Kits (``imx8mp-evk``, ``imx8mm-evk``)
==================================================================================

The ``imx8mp-evk`` and ``imx8mm-evk`` machine models the i.MX 8M Plus
and i.MX 8M Mini Evaluation Kits, based on i.MX 8M Plus and i.MX8M
Mini SoCs.

Supported devices
-----------------

The ``imx8mp-evk`` and ``imx8mm-evk`` machines implement the
following devices:

 * Up to 4 Cortex-A53 cores
 * 1 Cortex-M7 core (``imx8mp-evk`` only)
 * Generic Interrupt Controller (GICv3)
 * 4 UARTs
 * 3 USDHC Storage Controllers
 * 1 Designware PCI Express Controller
 * 1 Ethernet Controller
 * 2 Designware USB 3 Controllers
 * 5 GPIO Controllers
 * 6 I2C Controllers
 * 3 SPI Controllers
 * 3 Watchdogs
 * 6 General Purpose Timers
 * Secure Non-Volatile Storage (SNVS) including an RTC
 * Clock Tree
 * General Power Controller (GPC)
 * General Purpose Register (GPR)
 * System Reset Controller (SRC)
 * Messaging Unit (MU)

Boot options
------------

The ``imx8mp-evk`` and ``imx8mm-evk`` machines can start a Linux
kernel directly using the standard ``-kernel`` functionality.


Asymmetric Multiprocessing (AMP) Boot Recipe (``imx8mp-evk`` only)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``imx8mp-evk`` machine includes a Cortex-M7 core alongside the
Cortex-A53 cores, enabling Asymmetric Multiprocessing (AMP). The M7
firmware can be loaded from Linux using the remoteproc framework.

There are 2 control paths for Cortex-M7 on iMX8MP:-
1. Firmware-mediated (via SMC/ATF)
2. MMIO driven path (via SRC and GPR access)

``fsl,imx8mp-cm7-mmio`` exists specifically to select the MMIO path and avoid dependence on firmware interfaces that aren’t guaranteed in qemu.
This mode uses the SRC syscon block and the IOMUXC GPR for start/stop control.

Memory carveouts for resource table, vrings need to be specified in the ``imx8mp-evk-rpmsg.dts``.
Follow this application note to make the necessary changes - https://www.nxp.com/docs/en/application-note/AN5317.pdf

When Linux boots CM7 via remoteproc, the typical flow is:

1. Linux booted with imx8mp-evk-rpmsg.dtb
2. Linux loads the CM7 ELF into a reserved DDR region
3. Linux toggles the CM7 start/stop control (SRC/GPR CPUWAIT, etc.)
4. CM7 starts executing from that DDR entry


Prerequisites
~~~~~~~~~~~~~

To manually test Cortex-M7 firmware loading from Linux, the following
components are needed:

1. Linux kernel configuration to enable i.MX remoteproc support.
2. ``imx8mp-evk-rpmsg.dtb`` -  device tree that enables the Cortex-M7 remoteproc node, reserves DDR memory regions
   for the Cortex-M7 firmware, resource table,vrings and buffers.
3. A Cortex-M7 ELF firmware image linked to execute from DDR.

NXP application note AN5317 describes the required remoteproc and reserved-memory setup
for loading Cortex-M firmware from Linux.


Linux kernel configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~

The guest kernel needs remoteproc support. On official linux-imx kernels, this support is enabled by default.
When using Buildroot, verify that the kernel configuration enables the remoteproc and rpmsg options needed by the
i.MX remoteproc driver, for example:

.. code-block:: none

  CONFIG_REMOTEPROC=y
  CONFIG_IMX_REMOTEPROC=y
  CONFIG_RPMSG=y
  CONFIG_VIRTIO_RPMSG_BUS=y
  CONFIG_RPMSG_CHAR=y

Depending on the kernel version and configuration, some options may be selected
automatically by the i.MX remoteproc driver.



Device tree preparation
~~~~~~~~~~~~~~~~~~~~~~~

1. Refer to ``9.1 i.MX Linux rproc support`` of application note AN5317 to add the
   following nodes in ``imx8mp-evk-rpmsg.dts`` :-

   m7_ddr_alias
   m7_itcm
   m7_dtcm

2. Modify the compatible string of ``imx8mp-cm7`` node from ``fsl,imx8mn-cm7`` to ``fsl,imx8mp-cm7-mmio``
3. Add the following properties to the ``imx8mp-cm7`` node :-

   ``syscon = <&src>;``
   ``fsl,iomuxc-gpr = <&gpr>;``

   These references are needed by imx_rproc_mmio_detect_mode in i.MX remoteproc driver
   for M7 boot mode detection.


   Build ``imx8mp-evk-rpmsg.dtb`` from the above dts changes.



Building a Cortex-M7 ELF firmware
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A simple manual test is the MCUXpresso SDK UART polling example for the i.MX8MP
EVK. It prints to a UART and is therefore easy to observe from QEMU.

1. Follow this guide to set up MCUXpresso SDK for i.MX8MPEVK :- MCUXSDKIMX8MPGSUG[https://share.google/qD4D09FCkkydTurqp]
2. Build a DDR-linked Cortex-M7 ELF using the ARM GNU toolchain from the MCUXpresso SDK:-


.. code-block:: bash

  $ cd ${MCUX_SDK}/boards/evkmimx8mp/driver_examples/uart/polling/armgcc
  $ ./build_ddr_release.sh

As a result, ``iuart_polling_cm7.elf`` will be generated  in ``ddr_release`` folder.

Boot qemu i.MX8MP EVK machine with the updated linux kernel and ``imx8mp-evk-rpmsg.dtb``

3. Copy the ``iuart_polling_cm7.elf`` to ``/lib/firmware/`` path inside iMX8MPEVK qemu emulation.


Serial ports (UARTs)
''''''''''''''''''''

The i.MX 8M Plus EVK model provides four UARTs. QEMU connects each UART to a
host character backend using the ``-serial`` option. This option can be used
multiple times to create and wire multiple serial ports.

The ``-serial`` options are positional:

* the 1st ``-serial ...`` maps to ``serial0`` (UART1)
* the 2nd ``-serial ...`` maps to ``serial1`` (UART2)
* the 3rd ``-serial ...`` maps to ``serial2`` (UART3)
* the 4th ``-serial ...`` maps to ``serial3`` (UART4)

Example usage:- To enable serial console for the official M7 mcuxpresso sdk driver example - driver_examples/uart/polling which uses UART4, use:-

.. code-block:: bash

  -serial null -serial stdio -serial null -serial pty:/tmp/imx8mp-uart4

This will create a symlink /tmp/imx8mp-uart4 pointed to the allocated PTY. On a different tab the console for UART4 can be opened using the following:-

.. code-block:: bash

  $ screen /tmp/imx8mp-uart4 115200


Starting QEMU for Cortex-M7 remoteproc testing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. Execute the following command to start i.MX8MPEVK emulation:-

.. code-block:: bash

  $ qemu-system-aarch64 -M imx8mp-evk \
      -display none -serial null -serial stdio -serial null -serial /tmp/imx8mp-uart4 \
      -kernel Image \
      -dtb imx8mp-evk-rpmsg.dtb \
      -append "root=/dev/mmcblk2p2" \
      -drive file=sdcard.img,if=sd,bus=2,format=raw,id=mmcblk2

2. On a new tab execute the following to open a console:-

.. code-block:: bash

  $ screen /tmp/imx8mp-uart4 115200

3. Execute the following commands inside emulation to load the firmware elf:-

.. code-block:: bash

   $ echo iuart_polling_cm7.elf > /sys/class/remoteproc/remoteproc0/firmware
   $ echo start > /sys/class/remoteproc/remoteproc0/state


On the tab where the console is opened, you will observe the UART logs. The characters
typed from the keyboard will echo on the console.


Note:-

Only DDR-linked bare-metal ELF images are currently supported by QEMU
emulation. If the firmware is linked for a vector table base address other than
``0x80000000``, configure the Cortex-M7 vector base using the SoC property
``cm7-vector-base``:-

.. code-block:: bash

  -global fsl-imx8mp.cm7-vector-base=0x80000000

If this property is not provided, QEMU uses ``0x80000000`` by default.



Direct Linux Kernel Boot
''''''''''''''''''''''''

Probably the easiest way to get started with a whole Linux system on the machine
is to generate an image with Buildroot. Version 2024.11.1 is tested at the time
of writing and involves two steps. First run the following commands in the
toplevel directory of the Buildroot source tree:

For i.MX 8M Plus EVK:

.. code-block:: bash

  $ make freescale_imx8mpevk_defconfig
  $ make

For i.MX 8M Mini EVK:

.. code-block:: bash

  $ make freescale_imx8mmevk_defconfig
  $ make

Once finished successfully there is an ``output/image`` subfolder. Navigate into
it and resize the SD card image to a power of two:

.. code-block:: bash

  $ qemu-img resize sdcard.img 256M

Now that everything is prepared the machine can be started as follows:

For i.MX 8M Plus EVK:

.. code-block:: bash

  $ qemu-system-aarch64 -M imx8mp-evk \
      -display none -serial null -serial stdio -serial null -serial /tmp/imx8mp-uart4 \
      -kernel Image \
      -dtb imx8mp-evk.dtb \
      -append "root=/dev/mmcblk2p2" \
      -drive file=sdcard.img,if=sd,bus=2,format=raw,id=mmcblk2

For i.MX 8M Mini EVK:

.. code-block:: bash

  $ qemu-system-aarch64 -M imx8mm-evk -smp 4 -m 2G \
      -display none -serial null -serial stdio \
      -kernel Image \
      -dtb imx8mm-evk.dtb \
      -append "root=/dev/mmcblk2p2" \
      -drive file=sdcard.img,if=sd,bus=2,format=raw,id=mmcblk2

KVM Acceleration
----------------

To enable hardware-assisted acceleration via KVM, append
``-accel kvm`` to the command line. While this speeds up performance
significantly, be aware of the following limitations:

* The ``imx8mp-evk`` and ``imx8mm-evk`` machines are not included
  under the "virtualization use case" of :doc:`QEMU's security
  policy </system/security>`.  This means that you should not trust that
  it can contain malicious guests, whether it is run using TCG or KVM.
  If you don't trust your guests and you're relying on QEMU to be the
  security boundary, you want to choose another machine such as
  ``virt``.
* Rather than Cortex-A53 CPUs, the same CPU type as the host's will be used.
  This is a limitation of KVM and may not work with guests with a tight
  dependency on Cortex-A53.
* No EL2 and EL3 exception levels are available which is also a KVM limitation.
  Direct kernel boot should work but running U-Boot, TF-A, etc. won't succeed.
