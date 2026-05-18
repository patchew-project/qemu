Beckhoff CX7200 (``beckhoff-cx7200``)
======================================
The Beckhoff CX7200 is based on the same architecture as the Xilinx Zynq A9.
The Zynq 7000 family is based on the AMD SoC architecture. These products
integrate a feature-rich dual or single-core Arm Cortex-A9 MPCore based
processing system (PS) and AMD programmable logic (PL) in a single device.
The Beckhoff Communication Controller (CCAT) can be found in the PL of Zynq.

You can find more detail about this board on
`the manufacturer's product page <https://www.beckhoff.com/en-en/products/ipc/embedded-pcs/cx7000-arm-r-cortex-r/cx7293.html?>`__.
The SoC is documented in the
`Zynq 7000 Technical Reference manual <https://docs.amd.com/r/en-US/ug585-zynq-7000-SoC-TRM>`__.

The CX7200 supports the following:

- Arm Cortex-A9 MPCore CPU

  - Cortex-A9 CPUs
  - GIC v1 interrupt controller
  - Generic timer
  - Watchdog timer

- OCM 256KB
- SMC SRAM 64MB
- Zynq SLCR
- SPI x2
- QSPI
- UART
- TTC x2
- Gigabit Ethernet Controller x2
- SD Controller x2
- XADC
- Arm PrimeCell DMA Controller
- DDR Memory
- USB 2.0 x2
- DDR Controller
- Beckhoff Communication Controller (CCAT)

  - EEPROM Interface
  - DMA Controller

The following is not supported:
    - I2C

Running
"""""""
Directly loading an ELF file to the CPU of the CX7200 to run f.e. TC/RTOS (based on FreeRTOS):

.. code-block:: bash

  $ qemu-system-arm -M beckhoff-cx7200 \
        -device loader,file=CX7200_Zynq_Fsbl.elf \
        -display none


For setting the EEPROM content of the CCAT provide the following on the command line:

.. code-block:: bash

        -drive file=eeprom.bin,format=raw,id=ccat-eeprom

The size of eeprom.bin must be aligned to a power of 2 and bigger than 256 bytes.
