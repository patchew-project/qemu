/*
 * AArch64 system register helpers
 *
 * Based on the helpers from Linux
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define __stringify_1(x...) #x
#define __stringify(x...)   __stringify_1(x)

#define read_sysreg(r) ({                                           \
            uint64_t __val;                                         \
            asm volatile("mrs %0, " __stringify(r) : "=r" (__val)); \
            __val;                                                  \
})

#define write_sysreg(v, r) do {                     \
        uint64_t __val = (uint64_t)(v);             \
        asm volatile("msr " __stringify(r) ", %x0"  \
                 : : "rZ" (__val));                 \
} while (0)

#define isb() asm volatile("isb" : : : "memory")
#define dsb(opt) asm volatile("dsb " #opt : : : "memory")

/*
 * SCTLR_EL1 Bits
 */
#define SCTLR_EL1_M      (1ULL << 0)  /* enable MMU for EL0/1 */
#define SCTLR_EL1_C      (1ULL << 2)  /* Data cachability control */
#define SCTLR_EL1_SA     (1ULL << 3)  /* SP alignment check */
#define SCTLR_EL1_I      (1ULL << 12) /* Instruction cachability control */

/*
 * TCR_EL1 Bits
 */
#define TCR_EL1_T0SZ(s)  ((s) & 0x3fULL)
#define TCR_EL1_IRGN0_WBWA (3ULL << 8)
#define TCR_EL1_ORGN0_WBWA (3ULL << 10)
#define TCR_EL1_IPS_40BIT  (2ULL << 32)
#define TCR_EL1_TG0_4KB    (0ULL << 14)
