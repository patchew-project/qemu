====================
QEMU firmware images
====================

This folder contains the collection of sources for firmware (ROM / BIOS)
images which are used for the various machines that are emulated by QEMU.
See the individual sub-folders for more information like requirements for
building and license statements.

Pre-built binaries of these firmwares can be found in the "pc-bios" folder
of the main QEMU source tree. It can be browsed online here:

 https://gitlab.com/qemu-project/qemu/-/tree/master/pc-bios


Building
========

The main Makefile provides some targets for building the various firmware
images in an easy way. Run "make help" in this directory to get a list of
available build targets.

Note that you might need to install an appropriate cross-compiler for
compiling certain targets first.
