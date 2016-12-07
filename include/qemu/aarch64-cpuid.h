#ifndef QEMU_AARCH64_CPUID_H
#define QEMU_AARCH64_CPUID_H

#if defined(__aarch64__) && defined(CONFIG_LINUX)
#define MIDR_IMPLEMENTER_SHIFT  24
#define MIDR_IMPLEMENTER_MASK   (0xffULL << MIDR_IMPLEMENTER_SHIFT)
#define MIDR_ARCHITECTURE_SHIFT 16
#define MIDR_ARCHITECTURE_MASK  (0xf << MIDR_ARCHITECTURE_SHIFT)
#define MIDR_PARTNUM_SHIFT      4
#define MIDR_PARTNUM_MASK       (0xfff << MIDR_PARTNUM_SHIFT)

#define MIDR_CPU_PART(imp, partnum) \
        (((imp)                 << MIDR_IMPLEMENTER_SHIFT)  | \
        (0xf                    << MIDR_ARCHITECTURE_SHIFT) | \
        ((partnum)              << MIDR_PARTNUM_SHIFT))

#define ARM_CPU_IMP_CAVIUM        0x43
#define CAVIUM_CPU_PART_THUNDERX  0x0A1

#define MIDR_THUNDERX_PASS2  \
               MIDR_CPU_PART(ARM_CPU_IMP_CAVIUM, CAVIUM_CPU_PART_THUNDERX)
#define CPU_MODEL_MASK  (MIDR_IMPLEMENTER_MASK | MIDR_ARCHITECTURE_MASK | \
                         MIDR_PARTNUM_MASK)

uint64_t get_aarch64_cpu_id(void);
bool is_thunderx_pass2_cpu(void);
#else
static inline uint64_t get_aarch64_cpu_id(void)
{
    return 0;
}

static inline bool is_thunderx_pass2_cpu(void)
{
    return false;
}
#endif
#endif
