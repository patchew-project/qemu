TARGET_ARCH=riscv32
TARGET_BASE_ARCH=riscv
TARGET_XML_FILES= gdb-xml/riscv-32bit-cpu.xml gdb-xml/riscv-32bit-fpu.xml gdb-xml/riscv-64bit-fpu.xml gdb-xml/riscv-32bit-virtual.xml
# needed by boot.c
TARGET_NEED_FDT=y
TARGET_LONG_BITS=32
TARGET_USE_LEGACY_NATIVE_ENDIAN_API=y
