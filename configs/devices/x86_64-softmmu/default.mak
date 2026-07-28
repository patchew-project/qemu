# Default configuration for x86_64-softmmu

include ../i386-softmmu/default.mak

# Enable I2C slave models used by the xiic-fpga-i2c qtest.
CONFIG_AT24C=y
CONFIG_TMP105=y
CONFIG_TMP421=y
