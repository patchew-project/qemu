.. _direct_005flinux_005fboot:

Direct Linux Boot
-----------------

This section explains how to launch a Linux kernel inside QEMU without
having to make a full bootable image. It is very useful for fast Linux
kernel testing.

The syntax is:

.. parsed-literal::

   |qemu_system| -kernel bzImage -drive file=rootdisk.img,format=raw -append "root=/dev/sda"

Use ``-kernel`` to provide the Linux kernel image and ``-append`` to
give the kernel command line arguments. The ``-initrd`` option can be
used to provide an INITRD image.

The ``-shim`` option specifies the shim.efi binary.  This is needed
when using direct kernel boot with UEFI secure boot enabled.  The
verification chain used by linux distros requires shim.efi.  Typically
shim.efi is signed by micsosoft and verified by the firmware.  The
linux kernel is signed by the distro and is verified by shim.efi.  So
without shim.efi in the loop secure boot verification will not work.
Usually you can find shim.efi as ``EFI/BOOT/BOOT{X64,AA64}.EFI`` on
distro install media.

If you do not need graphical output, you can disable it and redirect the
virtual serial port and the QEMU monitor to the console with the
``-nographic`` option. The typical command line is:

.. parsed-literal::

   |qemu_system| -kernel bzImage -drive file=rootdisk.img,format=raw \
                    -append "root=/dev/sda console=ttyS0" -nographic

Use :kbd:`Ctrl+a c` to switch between the serial console and the monitor (see
:ref:`GUI_keys`).
