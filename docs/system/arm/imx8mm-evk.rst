NXP i.MX 8MM Evaluation Kit (``imx8mm-evk``)
================================================

The ``imx8mm-evk`` machine models the i.MX 8M Mini Evaluation Kit, based on an
i.MX 8MM SoC.

Supported devices
-----------------

The ``imx8mm-evk`` machine implements the following devices:

 * Up to 4 Cortex-A53 cores
 * Generic Interrupt Controller (GICv3)
 * 4 UARTs
 * 3 USDHC Storage Controllers
 * Secure Non-Volatile Storage (SNVS) including an RTC
 * Clock Tree

Boot options
------------

The ``imx8mm-evk`` machine can start a Linux kernel directly using the standard
``-kernel`` functionality.

Direct Linux Kernel Boot
''''''''''''''''''''''''

Probably the easiest way to get started with a whole Linux system on the machine
is to generate an image with Buildroot. Version 2024.11.1 is tested at the time
of writing and involves two steps. First run the following commands in the
toplevel directory of the Buildroot source tree:

.. code-block:: bash

  $ make freescale_imx8mmevk_defconfig
  $ make

Once finished successfully there is an ``output/image`` subfolder. Navigate into
it and resize the SD card image to a power of two:

.. code-block:: bash

  $ qemu-img resize sdcard.img 256M

Now that everything is prepared the machine can be started as follows:

.. code-block:: bash

  $ qemu-system-aarch64 -M imx8mm-evk -smp 4 -m 3G \
      -display none -serial null -serial stdio \
      -kernel Image \
      -dtb imx8mm-evk.dtb \
      -append "root=/dev/mmcblk2p2" \
      -drive file=sdcard.img,if=sd,bus=2,format=raw,id=mmcblk2


KVM Acceleration
----------------

Be aware of the following limitations:

* The ``imx8mm-evk`` machine is not included under the "virtualization use case"
  of :doc:`QEMU's security policy </system/security>`. This means that you
  should not trust that it can contain malicious guests, whether it is run
  using TCG or KVM. If you don't trust your guests and you're relying on QEMU to
  be the security boundary, you want to choose another machine such as ``virt``.
* Rather than Cortex-A53 CPUs, the same CPU type as the host's will be used.
  This is a limitation of KVM and may not work with guests with a tight
  dependency on Cortex-A53.
* No EL2 and EL3 exception levels are available which is also a KVM limitation.
  Direct kernel boot should work but running U-Boot, TF-A, etc. won't succeed.
