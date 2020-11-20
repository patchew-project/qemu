Sharp XScale-based PDA models (``tosa``, ``spitz``, ``akita``, ``borzoi``, ``terrier``)
=======================================================================================

The Sharp Zaurus SL-6000 (``tosa``), released in 2005, was a PDA based on the
PXA255.

The XScale-based clamshell PDA models (\"Spitz\", \"Akita\", \"Borzoi\"
and \"Terrier\") emulation includes the following peripherals:

-  Intel PXA255/PXA270 System-on-chip (ARMv5TE core)

-  NAND Flash memory - not in \"Tosa\"

-  IBM/Hitachi DSCM microdrive in a PXA PCMCIA slot - not in \"Akita\"

-  On-chip OHCI USB controller - not in \"Tosa\"

-  On-chip LCD controller

-  On-chip Real Time Clock

-  TI ADS7846 touchscreen controller on SSP bus

-  Maxim MAX1111 analog-digital converter on |I2C| bus

-  GPIO-connected keyboard controller and LEDs

-  Secure Digital card connected to PXA MMC/SD host

-  Three on-chip UARTs

-  WM8750 audio CODEC on |I2C| and |I2S| busses
