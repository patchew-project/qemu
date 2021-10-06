# Default configuration for riscv128-softmmu

# Uncomment the following lines to disable these optional devices:
#
#CONFIG_PCI_DEVICES=n
# No does not seem to be an option for these two parameters
CONFIG_SEMIHOSTING=y
CONFIG_ARM_COMPATIBLE_SEMIHOSTING=y

# Boards:
#
CONFIG_SPIKE=n
CONFIG_SIFIVE_E=n
CONFIG_SIFIVE_U=n
CONFIG_RISCV_VIRT=y
CONFIG_MICROCHIP_PFSOC=n
CONFIG_SHAKTI_C=n
