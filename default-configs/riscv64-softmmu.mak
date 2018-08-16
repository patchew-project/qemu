# Default configuration for riscv-softmmu

CONFIG_SERIAL=y
CONFIG_VIRTIO_MMIO=y
include virtio.mak

CONFIG_CADENCE=y

CONFIG_PCI=y
CONFIG_PCI_GENERIC=y
