/*
 * Simple Virtual Timer Tests
 *
 * Note: kvm-unit-tests has a much more comprehensive exercising of
 * the timer sub-system. However this test case can tweak _EL2 values
 * to trigger bugs which can't be done with that.
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <inttypes.h>
#include <minilib.h>

/* grabbed from Linux */
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

/* Physical Counter */
static uint64_t last_pct;
/* Timer Values */
static uint32_t last_phys_tval;
static uint32_t last_virt_tval;

static void dump_status(void)
{
    uint64_t pct = read_sysreg(cntpct_el0);
    uint32_t phys_tval = read_sysreg(cntp_tval_el0);
    uint32_t virt_tval = read_sysreg(cntv_tval_el0);

    ml_printf("timer values:\n");
    /* the physical timer monotonically increments */
    ml_printf("cntpct_el0=%ld (+%ld)\n", pct, pct - last_pct);
    /* the various tvals decrement based on cval */
    ml_printf("cntp_tval_el0=%ld (-%ld)\n", phys_tval,
              last_phys_tval - phys_tval);
    ml_printf("cntv_tval_el0=%ld (-%ld)\n", virt_tval,
              last_virt_tval - virt_tval);

    last_pct = pct;
    last_phys_tval = phys_tval;
    last_virt_tval = virt_tval;
}

int main(void)
{
    int i;

    ml_printf("VTimer Tests\n");

    dump_status();

    ml_printf("Tweaking voff_el2 and cval\n");
    write_sysreg(cntvoff_el2, 1);
    write_sysreg(cntv_cval_el0, -1);

    dump_status();

    ml_printf("Enabling timer IRQs\n");
    write_sysreg(cntv_ctl_el0, 1);
    /* for bug 1859021 we hang here */

    dump_status();

    ml_printf("End of Vtimer test\n");
    return 0;
}
