Experimental RISC-V Server Platform Reference board (``rvsp-ref``)
==================================================================

The RISC-V Server Platform specification `spec`_ defines a standardized
set of hardware and software capabilities that portable system software,
such as OS and hypervisors, can rely on being present in a RISC-V server
platform. This machine aims to emulate this specification, providing
an environment for firmware/OS development and testing.

The main features included in rvsp-ref are:

*  a new CPU type rvsp-ref CPU for server platform compliance
* AIA
* PCIe AHCI
* PCIe NIC
* No virtio mmio bus
* No fw_cfg device
* No ACPI table
* Minimal device tree nodes

The board is being provisioned as *experimental* because QEMU isn't
100% compliant with the specification at this moment - we do not have
support for the mandatory 'sdext' extension. The existence of the board
is beneficial to the development of the ecossystem around the specification,
so we're choosing the make the board available even in an incomplete state.
When 'sdext' is implemented we'll remove the 'experimental' tag from it.

.. _spec: https://github.com/riscv-non-isa/riscv-server-platform
