/*
 * Test short-descriptor domain fault vs. L2-descriptor validity ordering.
 * (GitLab issue #4233)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Sets up a non-LPAE (short-descriptor) page table:
 *  - L1[0]       section, identity map of the low 1MB (our own code/data),
 *                domain 0.
 *  - L1[4]       coarse (page-table) entry for VA 0x00400000, domain 1,
 *                pointing at an L2 table that is left entirely zeroed
 *                (every L2 entry therefore decodes as "invalid").
 *  - DACR: domain 0 = Client (viable), domain 1 = No Access.
 *
 * Then reads VA 0x00400000. Spec (ARM DDI 0100I/E, Figure B4-2 and
 * ARM DDI 0406C): the domain is checked only after a valid second-level
 * descriptor is returned. Since the L2 entry is invalid (0), a spec-faithful
 * walker must report a page Translation Fault (DFSR[3:0] = 0x7),
 * independent of DACR[1].
 *
 * QEMU previously checked the domain from the L1 descriptor before fetching
 * L2, incorrectly reporting a Domain Fault (DFSR[3:0] = 0xb).
 */

#include <stdint.h>

#define VICTIM_VA 0x00400000u

#ifdef V6_SHORT_DESC_TEST
#define L1_SECTION_EXEC_FLAGS ((3u << 10) | 0x2u)
#define SCTLR_TEST_FORMAT_BIT (1u << 23) /* XP: use v6 short descriptors */
#else
#define L1_SECTION_EXEC_FLAGS ((3u << 10) | (1u << 4) | 0x2u)
#define SCTLR_TEST_FORMAT_BIT 0u
#endif

static inline void semihost_write0(const char *str)
{
    register uint32_t r0 __asm__("r0") = 0x04; /* SYS_WRITE0 */
    register uint32_t r1 __asm__("r1") = (uint32_t)(uintptr_t)str;
    __asm__ volatile ("svc 0x123456" : : "r"(r0), "r"(r1) : "memory");
}

static inline void semihost_exit(int status)
{
    register uint32_t r0 __asm__("r0") = 0x18; /* SYS_EXIT */
    register uint32_t r1 __asm__("r1") = status ? 0x20024 : 0x20026;
    __asm__ volatile ("svc 0x123456" : : "r"(r0), "r"(r1) : "memory");
}

static void puthex32(uint32_t v)
{
    static const char hex[] = "0123456789abcdef";
    char buf[9];
    int i;

    for (i = 7; i >= 0; i--) {
        buf[i] = hex[v & 0xf];
        v >>= 4;
    }
    buf[8] = '\0';
    semihost_write0(buf);
}

static void report(const char *label, uint32_t v)
{
    semihost_write0(label);
    puthex32(v);
    semihost_write0("\n");
}

/* L1: 4096 entries, 16KB, 16KB-aligned. L2: 256 entries, 1KB, 1KB-aligned. */
static uint32_t l1_table[4096] __attribute__((aligned(16384)));
static uint32_t l2_table[256] __attribute__((aligned(1024)));

void c_main(void)
{
    uint32_t i;
    uint32_t l2_phys = (uint32_t)(uintptr_t)l2_table;
    uint32_t sctlr;

    semihost_write0("=== Domain fault ordering test ===\n");

    for (i = 0; i < 4096; i++) {
        l1_table[i] = 0;
    }
    for (i = 0; i < 256; i++) {
        l2_table[i] = 0;
    }

    /* L1[0]: identity section for our code/data/stack, domain 0. */
    l1_table[0] = 0x00000000u | (0u << 5) | L1_SECTION_EXEC_FLAGS;

    /* L1[4]: coarse entry for VICTIM_VA, domain 1, L2 all-invalid. */
    l1_table[VICTIM_VA >> 20] =
        (l2_phys & 0xfffffc00u) | (1u << 5) | (1u << 4) | 0x1u;

    report("L1 table @ 0x", (uint32_t)(uintptr_t)l1_table);
    report("L2 table @ 0x", l2_phys);
    report("L1[victim] = 0x", l1_table[VICTIM_VA >> 20]);

    /* DACR: domain0 = Client (0b01), domain1 = No Access (0b00). */
    __asm__ volatile ("mcr p15, 0, %0, c3, c0, 0" : : "r"(0x00000001u));

    /* TTBR0 */
    __asm__ volatile ("mcr p15, 0, %0, c2, c0, 0"
                      : : "r"((uint32_t)(uintptr_t)l1_table));

    /* Invalidate unified TLB */
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));

    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    report("SCTLR before MMU on = 0x", sctlr);
    sctlr |= 1u | SCTLR_TEST_FORMAT_BIT;
    __asm__ volatile (
        "mcr p15, 0, %0, c1, c0, 0\n"
        "nop\n\tnop\n\tnop\n\tnop\n"
        : : "r"(sctlr));

    semihost_write0("MMU enabled, reading victim VA (expect abort)...\n");
    {
        /* Trigger data abort at VICTIM_VA */
        volatile uint32_t *victim = (volatile uint32_t *)VICTIM_VA;
        uint32_t v = *victim;
        report("UNEXPECTED: read succeeded, value = 0x", v);
        semihost_exit(1);
    }
}

void report_fault(uint32_t dfsr, uint32_t dfar)
{
    uint32_t fs = dfsr & 0xfu;

    report("DFAR = 0x", dfar);
    report("DFSR = 0x", dfsr);
    report("DFSR[3:0] = 0x", fs);

    if (dfar == VICTIM_VA && fs == 0x7) {
        semihost_write0("PASS: level-2 translation fault (0x7)\n");
        semihost_exit(0);
    } else {
        semihost_write0("FAIL: expected translation fault (0x7), "
                        "got fault status 0x");
        puthex32(fs);
        semihost_write0("\n");
        semihost_exit(1);
    }
}
