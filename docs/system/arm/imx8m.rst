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

Asymmetric Multiprocessing (AMP) Boot (``imx8mp-evk`` only)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

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


If you build a Cortex-M7 bare-metal firmware elf that is linked for a vector
table base address other than the default 0x80000000, configure the CM7 vector base via the SoC property
``cm7-vector-base``.

Note:-Currently only DDR-linked bare-metal binaries are supported in qemu emulation.

This can be set using a global property:

.. code-block:: bash

  -global fsl-imx8mp.cm7-vector-base=0x80000000

In the absence of the above global property in qemu invocation, by default 0x80000000 will be used.


To run the i.MX 8M Plus model with the Cortex-M7 core enabled(4x A53 + 1x M7), start QEMU with

.. code-block:: bash

  -smp 4,maxcpus=5 -global fsl-imx8mp.enable-cm7=on


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


Once Linux is running, the M7 firmware can be loaded and started via the remoteproc interface:

.. code-block:: bash

  # echo <firmware_name>.elf > /sys/class/remoteproc/remoteproc0/firmware
  # echo start > /sys/class/remoteproc/remoteproc0/state


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
      -smp 4,maxcpus=5 -global fsl-imx8mp.enable-cm7=on \
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
