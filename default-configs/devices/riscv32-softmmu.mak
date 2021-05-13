# Default configuration for riscv32-softmmu

# TODO: semihosting is always required - move to default-configs/targets/
CONFIG_ARM_COMPATIBLE_SEMIHOSTING=y

# Uncomment the following lines to disable these optional devices:
#
#CONFIG_PCI_DEVICES=n

# Boards:
#
CONFIG_SPIKE=y
CONFIG_SIFIVE_E=y
CONFIG_SIFIVE_U=y
CONFIG_RISCV_VIRT=y
CONFIG_OPENTITAN=y
