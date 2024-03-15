#ifndef QEMU_ARCH_INIT_H
#define QEMU_ARCH_INIT_H

typedef enum QemuArchBit {
    QEMU_ARCH_BIT_ALPHA         = 0,
    QEMU_ARCH_BIT_ARM           = 1,
    QEMU_ARCH_BIT_CRIS          = 2,
    QEMU_ARCH_BIT_I386          = 3,
    QEMU_ARCH_BIT_M68K          = 4,
    QEMU_ARCH_BIT_MICROBLAZE    = 6,
    QEMU_ARCH_BIT_MIPS          = 7,
    QEMU_ARCH_BIT_PPC           = 8,
    QEMU_ARCH_BIT_S390X         = 9,
    QEMU_ARCH_BIT_SH4           = 10,
    QEMU_ARCH_BIT_SPARC         = 11,
    QEMU_ARCH_BIT_XTENSA        = 12,
    QEMU_ARCH_BIT_OPENRISC      = 13,
    QEMU_ARCH_BIT_TRICORE       = 16,
    QEMU_ARCH_BIT_NIOS2         = 17,
    QEMU_ARCH_BIT_HPPA          = 18,
    QEMU_ARCH_BIT_RISCV         = 19,
    QEMU_ARCH_BIT_RX            = 20,
    QEMU_ARCH_BIT_AVR           = 21,
    QEMU_ARCH_BIT_HEXAGON       = 22,
    QEMU_ARCH_BIT_LOONGARCH     = 23,

    QEMU_ARCH_BIT_LAST          = QEMU_ARCH_BIT_LOONGARCH
} QemuArchBit;

const char *cpu_typename_by_arch_bit(QemuArchBit arch_bit);

enum QemuArchMask {
    QEMU_ARCH_ALL = -1,
    QEMU_ARCH_ALPHA             = (1 << QEMU_ARCH_BIT_ALPHA),
    QEMU_ARCH_ARM               = (1 << QEMU_ARCH_BIT_ARM),
    QEMU_ARCH_CRIS              = (1 << QEMU_ARCH_BIT_CRIS),
    QEMU_ARCH_I386              = (1 << QEMU_ARCH_BIT_I386),
    QEMU_ARCH_M68K              = (1 << QEMU_ARCH_BIT_M68K),
    QEMU_ARCH_MICROBLAZE        = (1 << QEMU_ARCH_BIT_MICROBLAZE),
    QEMU_ARCH_MIPS              = (1 << QEMU_ARCH_BIT_MIPS),
    QEMU_ARCH_PPC               = (1 << QEMU_ARCH_BIT_PPC),
    QEMU_ARCH_S390X             = (1 << QEMU_ARCH_BIT_S390X),
    QEMU_ARCH_SH4               = (1 << QEMU_ARCH_BIT_SH4),
    QEMU_ARCH_SPARC             = (1 << QEMU_ARCH_BIT_SPARC),
    QEMU_ARCH_XTENSA            = (1 << QEMU_ARCH_BIT_XTENSA),
    QEMU_ARCH_OPENRISC          = (1 << QEMU_ARCH_BIT_OPENRISC),
    QEMU_ARCH_TRICORE           = (1 << QEMU_ARCH_BIT_TRICORE),
    QEMU_ARCH_NIOS2             = (1 << QEMU_ARCH_BIT_NIOS2),
    QEMU_ARCH_HPPA              = (1 << QEMU_ARCH_BIT_HPPA),
    QEMU_ARCH_RISCV             = (1 << QEMU_ARCH_BIT_RISCV),
    QEMU_ARCH_RX                = (1 << QEMU_ARCH_BIT_RX),
    QEMU_ARCH_AVR               = (1 << QEMU_ARCH_BIT_AVR),
    QEMU_ARCH_HEXAGON           = (1 << QEMU_ARCH_BIT_HEXAGON),
    QEMU_ARCH_LOONGARCH         = (1 << QEMU_ARCH_BIT_LOONGARCH),
};

extern const uint32_t arch_type;

void qemu_init_arch_modules(void);

#endif
