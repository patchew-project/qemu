TARGET_ARCH=riscv64
TARGET_BASE_ARCH=riscv
TARGET_SUPPORTS_MTTCG=y
TARGET_XML_FILES= gdb-xml/riscv-64bit-cpu.xml gdb-xml/riscv-32bit-fpu.xml gdb-xml/riscv-64bit-fpu.xml gdb-xml/riscv-64bit-virtual.xml
# needed by boot.c
TARGET_NEED_FDT=y
