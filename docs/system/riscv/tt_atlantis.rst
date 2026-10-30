Tenstorrent Atlantis (``tt-atlantis``)
======================================

The Tenstorrent Atlantis platform is a collaboration between Tenstorrent
and CoreLab Technology. It is based on the Atlantis SoC, which includes
the Ascalon-X CPU and other IP from Tenstorrent and CoreLab Technology.

The Tenstorrent Ascalon-X is a high performance 64-bit RVA23 compliant
RISC-V CPU.

tt-atlantis QEMU model features
-------------------------------

* 8-core Ascalon-X CPU Cluster
* RV32 Real-time CPU (RCPU)
* RISC-V compliant Advanced Interrupt Architecture
* DW8250 compatible UART x5
* 4 PRCMs (Clock and Reset Controllers)

Boot options
------------

* ``rcpu`` enables the RCPU model to run alongside the Ascalon cluster and is
  disabled by default. When the RCPU is enabled, ``UART0``, the RCPU's console,
  will be mapped to the second -serial on the command line.
* ``bootrom`` binary to be loaded into mask ROM and run by the RCPU (no effect if the
  rcpu is not enabled). If no bootrom is provided the RCPU will not be powered on.

To enable the RCPU model with a custom binary and the RCPU console on stdio:

.. code-block:: bash

  -M tt-atlantis,rcpu=on,bootrom=../path/to/binary.bin -serial null -serial stdio

* ``-smp cpus=`` The smp cpu field includes the RCPU count. When rcpu=on, the
  number of Ascalon harts created is smp.cpus - 1.  When rcpu=off, the number
  of Ascalon harts created is smp.cpus.

To run with only one Ascalon hart and no RCPU:

.. code-block:: bash

  -M tt-atlantis,rcpu=off -smp cpus=1



Known limitations
-----------------

The QEMU tt-atlantis machine does not yet model every device on the
real platform. Notably:

* There is no PCI host bridge, so virtio-pci devices cannot be
  attached. Boots that need block storage must use ``-initrd`` with
  an initramfs.
* The Atlantis platform has 5 UARTs but only ``UART1`` and, optionally,
  ``UART0`` are currently exposed on the command line serial interface.
  ``UART1`` is the Ascalon console and ``UART0`` is the RCPU console.

Supported software
------------------

The Tenstorrent Ascalon CPUs avoid proprietary or non-standard
extensions, so compatibility with existing software is generally
good. The QEMU tt-atlantis machine works with upstream OpenSBI
and Linux with default configurations.

The development board hardware will require some implementation
specific setup in firmware which is being developed and may
become a requirement or option for the tt-atlantis machine.

Debugging
----------

Ascalon and RCPU are on separate CPUClusters which allows gdb to put Ascalon
and RCPU on separate inferiors. This prevents issues with switching between
RV32 (RCPU) and RV64 (Ascalon) targets.

By default, gdb will connect to inferior 1, the Ascalon cluster and the RCPU
will be on inferior 2.

gdb does not handle multiple inferiors by default, see :ref:`gdb multicore`
for how to connect to inferior 2.
