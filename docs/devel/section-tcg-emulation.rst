*************
TCG Emulation
*************

QEMU was originally built as an emulator capable of running binaries
for one architecture on another. The following sections describe the
internals of how the Just In Time (JIT) Tiny Code Generator (TCG)
works. You only really need to read this if you are interested in
adding new architectures or fixing existing architecture emulation.


.. toctree::
   :maxdepth: 2

   tcg
   multi-thread-tcg
   tcg-icount
   decodetree
   tcg-plugins
