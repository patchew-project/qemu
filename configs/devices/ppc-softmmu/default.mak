# Default configuration for ppc-softmmu

# Uncomment the following lines to disable these optional devices:
# CONFIG_PCI_DEVICES=n
# CONFIG_TEST_DEVICES=n

# For embedded PPCs:
CONFIG_E500PLAT=y
CONFIG_MPC8544DS=y
CONFIG_PPC405=y
CONFIG_PPC440=y
CONFIG_VIRTEX=y

# For Sam460ex
CONFIG_SAM460EX=y

# For Macs
CONFIG_MAC_OLDWORLD=y
CONFIG_MAC_NEWWORLD=y

CONFIG_AMIGAONE=y
CONFIG_PEGASOS2=y

# For PReP
CONFIG_PREP=y
