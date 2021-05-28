#
# A version of the config that only supports 64bits and their devices.
# This doesn't quite eliminate all 32 bit devices as some boards like
# "virt" support both.
#

CONFIG_ARM_VIRT=y
CONFIG_XLNX_ZYNQMP_ARM=y
CONFIG_XLNX_VERSAL=y
CONFIG_SBSA_REF=y
