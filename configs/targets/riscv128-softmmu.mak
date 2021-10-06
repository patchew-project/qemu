#For now a raw copy of the riscv64 version as changing TARGET_ARCH to riscv64 might trigger to much stuff for now
TARGET_ARCH=riscv128
TARGET_BASE_ARCH=riscv
TARGET_SUPPORTS_MTTCG=y
TARGET_XML_FILES=gdb-xml/riscv-128bit-cpu.xml gdb-xml/riscv-32bit-fpu.xml gdb-xml/riscv-64bit-fpu.xml gdb-xml/riscv-128bit-virtual.xml
TARGET_NEED_FDT=y
