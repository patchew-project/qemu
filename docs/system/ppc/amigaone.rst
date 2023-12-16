Eyetech AmigaOne/Mai Logic Teron (``amigaone``)
===============================================

The ``amigaone`` model emulates an AmigaOne XE mainboard developed by Eyetech. Use
the executable ``qemu-system-ppc`` to simulate a complete system.


Emulated devices
----------------

 *  PowerPC 7457 v1.2 CPU
 *  Articia S north bridge
 *  VT82C686B south bridge
 *  PCI VGA compatible card


Preparation
-----------

A firmware binary is necessary for the boot process and is available at
https://www.hyperion-entertainment.com/index.php/downloads?view=files&parent=28.
It needs to be extracted with the following command:

.. code-block:: bash

  $ tail -c 524288 updater.image > u-boot-amigaone.bin

The firmware binary is unable to run QEMU‘s standard vgabios and
``VGABIOS-lgpl-latest.bin`` is needed instead. It can be downloaded from
http://www.nongnu.org/vgabios.


Running Linux
-------------

There are some Linux images under the following link that work on the
``amigaone`` machine:
https://sourceforge.net/projects/amigaone-linux/files/debian-installer/. To boot
the system run:

.. code-block:: bash

  $ qemu-system-ppc -M amigaone -bios u-boot-amigaone.bin \
                    -cdrom "A1 Linux Net Installer.iso" \
                    -device ati-vga,model=rv100,romfile=VGABIOS-lgpl-latest.bin

From the firmware menu that appears select ``Boot sequence`` →
``Amiga Multiboot Options`` and set ``Boot device 1`` to
``Onboard VIA IDE CDROM``. Then hit escape until the main screen appears again,
hit escape once more and from the exit menu that appears select either
``Save settings and exit`` or ``Use settings for this session only``. It may
take a long time loading the kernel into memory but eventually it boots and the
installer becomes visible.
