/*
 * WFX Instructions Test (WFI, WFE, WFIT, WFET)
 *
 * Copyright (c) 2026 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdbool.h>
#include <minilib.h>
#include "sysregs.h"
#include "gicv3.h"

#define TIMEOUT 200000

#define sev() asm volatile("sev" : : : "memory")
#define sevl() asm volatile("sevl" : : : "memory")
#define wfi() asm volatile("wfi" : : : "memory")
#define wfe() asm volatile("wfe" : : : "memory")
#define wfit(reg) asm volatile("wfit %0" : : "r" (reg) : "memory")
#define wfet(reg) asm volatile("wfet %0" : : "r" (reg) : "memory")

#define enable_irq()  asm volatile("msr daifclr, #2" : : : "memory")
#define disable_irq() asm volatile("msr daifset, #2" : : : "memory")

static bool check_elapsed(uint64_t start, uint64_t threshold, const char *test, bool more)
{
    uint64_t end = read_sysreg(cntvct_el0);
    uint64_t elapsed = end - start;
    if (more ? elapsed < threshold : elapsed > threshold) {
        ml_printf("FAILED: %s %s (%ld ticks)\n", test,
                  more ? "woke too early" : "slept despite SEV",
                  elapsed);
        return false;
    }
    ml_printf("PASSED (%ld ticks)\n", elapsed);
    return true;
}

int main(void)
{
    uint64_t start, timeout;

    gicv3_init();
    gicv3_enable_irq(27); /* Virtual Timer PPI */

    ml_printf("WFx[T] Tests\n");

    /*
     * 1. Test WFI with timer interrupt
     *
     * We don't have a full interrupt handler, but WFI should wake up
     * when the interrupt is pending even if we have it masked at the CPU.
     * PSTATE.I is set by boot code.
     *
     * We unmask interrupts here to ensure the CPU can take the minimal
     * exception handler defined in boot.S.
     */
    ml_printf("Testing WFI...");

    start = read_sysreg(cntvct_el0);
    write_sysreg(TIMEOUT, cntv_tval_el0);
    write_sysreg(1, cntv_ctl_el0); /* Enable timer, no mask */
    isb();

    enable_irq();
    wfi();
    disable_irq();

    if (!check_elapsed(start, TIMEOUT, "WFI", true)) {
        return 1;
    }

    /* Validate the timer fired and then disable for future tests */
    if (!read_sysreg(cntv_ctl_el0) & 0x4) {
        ml_printf("Time ISTATUS not set!\n");
        return 1;
    }
    write_sysreg(0, cntv_ctl_el0);

    /*
     * 2. Test WFE and SEV[L]
     *
     * There are two SEV instructions, the normal one is a broadcast
     * from any PE on the system, the other is local only.
     * Functionally they have the same effect (setting the event
     * register) and should be immediately consumed by the WFE.
     *
     * As we want to detect an early exit the sense of the timeout
     * check is reversed.
     */
    ml_printf("Testing WFE/SEV...");
    sev();
    start = read_sysreg(cntvct_el0);
    wfe();
    if (!check_elapsed(start, TIMEOUT, "WFE", false)) {
        return 1;
    }

    ml_printf("Testing WFE/SEVL...");
    sevl();
    start = read_sysreg(cntvct_el0);
    wfe();
    if (!check_elapsed(start, TIMEOUT, "WFE", false)) {
        return 1;
    }

    /*
     * 3. Test WFIT
     *
     * With the timer now disabled and no other IRQ sources firing the
     * WFIT instruction should timeout. Although the architecture
     * permits this being treated as a NOP we have enabled it.
     */
    ml_printf("Testing WFIT...");
    start = read_sysreg(cntvct_el0);
    timeout = start + TIMEOUT;
    wfit(timeout);
    if (!check_elapsed(start, TIMEOUT, "WFIT", true)) {
        return 1;
    }

    /*
     * 4. Test WFET
     *
     * Much like WFIT there are no IRQs to wake us up. However the
     * event_register is a latch so we must first consume the event
     * register with a normal WFE before we do the timeout version.
       */
    ml_printf("Testing WFET...");
    sev();
    wfe();
    start = read_sysreg(cntvct_el0);
    timeout = start + TIMEOUT;
    wfet(timeout);
    if (!check_elapsed(start, TIMEOUT, "WFET", true)) {
        return 1;
    }

    ml_printf("ALL WFX TESTS PASSED\n");
    return 0;
}
