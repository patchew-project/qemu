TARGET_ARCH=riscv128
TARGET_BASE_ARCH=riscv
# As long as we have no atomic accesses for aligned 128-bit addresses
TARGET_SUPPORTS_MTTCG=n
TARGET_XML_FILES=gdb-xml/riscv-64bit-cpu.xml gdb-xml/riscv-32bit-fpu.xml gdb-xml/riscv-64bit-fpu.xml gdb-xml/riscv-64bit-virtual.xml
TARGET_NEED_FDT=y
