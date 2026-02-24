/*
 * WFX Instructions Test (WFI, WFE, WFIT, WFET)
 *
 * Copyright (c) 2024 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <minilib.h>

#define __stringify_1(x...) #x
#define __stringify(x...)   __stringify_1(x)

#define read_sysreg(r) ({                                           \
            uint64_t __val;                                         \
            asm volatile("mrs %0, " __stringify(r) : "=r" (__val)); \
            __val;                                                  \
})

#define write_sysreg(r, v) do {                     \
        uint64_t __val = (uint64_t)(v);             \
        asm volatile("msr " __stringify(r) ", %x0"  \
                 : : "rZ" (__val));                 \
} while (0)

#define isb() asm volatile("isb" : : : "memory")
#define sev() asm volatile("sev" : : : "memory")
#define wfi() asm volatile("wfi" : : : "memory")
#define wfe() asm volatile("wfe" : : : "memory")
#define wfit(reg) asm volatile("wfit %0" : : "r" (reg) : "memory")
#define wfet(reg) asm volatile("wfet %0" : : "r" (reg) : "memory")

static void wait_ticks(uint64_t ticks)
{
    uint64_t start = read_sysreg(cntvct_el0);
    while ((read_sysreg(cntvct_el0) - start) < ticks) {
        /* spin */
    }
}

int main(void)
{
    uint64_t start, end, elapsed;
    uint64_t timeout;

    ml_printf("WFX Test\n");

    /* 1. Test WFI with timer interrupt */
    ml_printf("Testing WFI...");
    /* Setup virtual timer to fire in 100000 ticks (~2ms at 50MHz) */
    start = read_sysreg(cntvct_el0);
    write_sysreg(cntv_tval_el0, 100000);
    write_sysreg(cntv_ctl_el0, 1); /* Enable timer, no mask */
    isb();

    /*
     * We don't have a full interrupt handler, but WFI should wake up
     * when the interrupt is pending even if we have it masked at the CPU.
     * PSTATE.I is set by boot code.
     */
    wfi();
    end = read_sysreg(cntvct_el0);
    elapsed = end - start;
    if (elapsed < 100000) {
        ml_printf("FAILED: WFI woke too early (%ld ticks)\n", elapsed);
        return 1;
    }
    ml_printf("PASSED (elapsed %ld ticks)\n", elapsed);
    write_sysreg(cntv_ctl_el0, 0); /* Disable timer */

    /* 2. Test WFE and SEV */
    ml_printf("Testing WFE/SEV...");
    sev(); /* Set event register */
    start = read_sysreg(cntvct_el0);
    wfe(); /* Should return immediately */
    end = read_sysreg(cntvct_el0);
    elapsed = end - start;
    if (elapsed > 1000) { /* Should be very fast */
        ml_printf("FAILED: WFE slept despite SEV (%ld ticks)\n", elapsed);
        return 1;
    }
    ml_printf("PASSED\n");

    /* 3. Test WFIT */
    ml_printf("Testing WFIT...");
    start = read_sysreg(cntvct_el0);
    timeout = start + 200000;
    wfit(timeout);
    end = read_sysreg(cntvct_el0);
    elapsed = end - start;
    if (elapsed < 200000) {
        ml_printf("FAILED: WFIT woke too early (%ld ticks)\n", elapsed);
        return 1;
    }
    ml_printf("PASSED (elapsed %ld ticks)\n", elapsed);

    /* 4. Test WFET */
    ml_printf("Testing WFET...");
    start = read_sysreg(cntvct_el0);
    timeout = start + 200000;
    wfet(timeout);
    end = read_sysreg(cntvct_el0);
    elapsed = end - start;
    if (elapsed < 200000) {
        ml_printf("FAILED: WFET woke too early (%ld ticks)\n", elapsed);
        return 1;
    }
    ml_printf("PASSED (elapsed %ld ticks)\n", elapsed);

    ml_printf("ALL WFX TESTS PASSED\n");
    return 0;
}
