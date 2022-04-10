Allwinner sun4i-ss
==================

The ``sun4i-ss`` emulates the Allwinner cryptographic offloader
present on early Allwinner SoCs (A10, A10s, A13, A20, A33)
In qemu only A10 via the cubieboard machine is supported.

The emulated hardware is capable of handling the following algorithms:
- SHA1 and MD5 hash algorithms
- AES/DES/DES3 in CBC/ECB
- PRNG

The emulated hardware does not handle yet:
- CTS for AES
- CTR for AES/DES/DES3
- IRQ and DMA mode
Anyway the Linux driver also does not handle them yet.

The emulation needs a real crypto backend, for the moment only gnutls/nettle is supported.
So the device emulation needs qemu to be compiled with optionnal gnutls.

Emulation limit
---------------

PRNG:
The PRNG is not really emulated as its internal is not known.
It is replaced by the g_random_int function from glib.

SPEED:
The emulated hardware is ""faster"" than real hw as any write
already give results immediatly instead of a few delay in real HW.
