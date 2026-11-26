/*
 * WFIT/WFET timeout test, WFE wakeup after a failed store-exclusive
 *
 * Copyright (c) 2026 Google LLC
 * Author: Fuad Tabba <fuad.tabba@linux.dev>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <minilib.h>

/* from Linux's include/linux/stringify.h */
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

/* .inst forms of WFIT x0 and WFET x0, for assemblers without FEAT_WFxT */
static inline void wfit(uint64_t timeout)
{
    register uint64_t x0 asm("x0") = timeout;

    asm volatile(".inst 0xd5031020" : : "r" (x0) : "memory");
}

static inline void wfet(uint64_t timeout)
{
    register uint64_t x0 asm("x0") = timeout;

    asm volatile(".inst 0xd5031000" : : "r" (x0) : "memory");
}

/* virt machine, GICv2 */
#define GICD_BASE 0x08000000UL
#define GICC_BASE 0x08010000UL
#define GICD_CTLR 0x000
#define GICD_ISENABLER0 0x100
#define GICC_CTLR 0x000
#define GICC_PMR 0x004
#define VTIMER_PPI 27

static inline void mmio_write32(uintptr_t addr, uint32_t val)
{
    /* GIC registers: MMIO, MMU off at EL2 */
    *(volatile uint32_t *)addr = val;
}

static int test_one(const char *name, void (*wait)(uint64_t), uint64_t freq)
{
    uint64_t now, timeout, elapsed;
    int early = 0;

    /*
     * The virtual timer interrupt, 1s out, wakes the CPU even with
     * interrupts masked. The timeout, 10ms out, must be what wakes it.
     */
    now = read_sysreg(cntvct_el0);
    write_sysreg(cntv_cval_el0, now + freq);
    write_sysreg(cntv_ctl_el0, 1);
    timeout = now + freq / 100;

    do {
        wait(timeout);
        early++;
    } while (read_sysreg(cntvct_el0) < timeout && early < 1000);

    elapsed = read_sysreg(cntvct_el0) - now;
    write_sysreg(cntv_ctl_el0, 0);

    ml_printf("%s: woke after %ld ticks (%d wakes)\n", name, elapsed, early);
    if (early >= 1000) {
        ml_printf("FAIL: %s kept waking before its timeout\n", name);
        return 1;
    }
    if (elapsed > freq / 2) {
        ml_printf("FAIL: %s woke on the timer interrupt, not its timeout\n",
                  name);
        return 1;
    }
    return 0;
}

/*
 * At EL2 with virtualization enabled, QEMU's PSCI implementation uses the
 * SMC conduit.
 */
#define PSCI_CPU_ON 0xc4000003UL

static uint64_t psci_cpu_on(uint64_t mpidr, uint64_t entry)
{
    register uint64_t x0 asm("x0") = PSCI_CPU_ON;
    register uint64_t x1 asm("x1") = mpidr;
    register uint64_t x2 asm("x2") = entry;
    register uint64_t x3 asm("x3") = 0;

    asm volatile("smc #0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x3)
                 : "memory");
    return x0;
}

static uint32_t lock_word;
static uint32_t lock_step;

asm(".pushsection .bss\n"
    ".balign 16\n"
    "secondary_stack:\n"
    ".space 4096\n"
    "secondary_stack_end:\n"
    ".popsection\n"
    ".global secondary_entry\n"
    "secondary_entry:\n"
    "adrp x0, secondary_stack_end\n"
    "add x0, x0, :lo12:secondary_stack_end\n"
    "mov sp, x0\n"
    "b secondary_main\n");

/* CPU 1 takes the lock while CPU 0 is between LDAXR and STXR. */
static void __attribute__((used)) secondary_main(void)
{
    while (__atomic_load_n(&lock_step, __ATOMIC_ACQUIRE) != 1) {
    }
    __atomic_store_n(&lock_word, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&lock_step, 2, __ATOMIC_RELEASE);

    for (;;) {
        asm volatile("wfi");
    }
}

/*
 * Spin lock pattern: "LDAXR, STXR, on failure WFE" with a lock released
 * by a plain store and without SEV. The store-exclusive fails because
 * another CPU took the lock. That store cleared the global monitor of
 * this CPU, which generates a WFE wakeup event, so the following WFE
 * must complete at once.
 */
static int test_wfe_lock(uint64_t freq)
{
    uint64_t entry, now, elapsed;
    uint32_t val, status;

    asm("adrp %0, secondary_entry\n"
        "add %0, %0, :lo12:secondary_entry" : "=r" (entry));
    if (psci_cpu_on(1, entry) != 0) {
        ml_printf("FAIL: cannot start CPU 1\n");
        return 1;
    }

    /* Clear the event register */
    asm volatile("sevl; wfe");

    asm volatile("ldaxr %w0, [%1]" : "=r" (val) : "r" (&lock_word)
                 : "memory");
    __atomic_store_n(&lock_step, 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&lock_step, __ATOMIC_ACQUIRE) != 2) {
    }
    asm volatile("stxr %w0, %w2, [%1]" : "=&r" (status)
                 : "r" (&lock_word), "r" (1) : "memory");

    if (val != 0 || status == 0) {
        ml_printf("FAIL: store-exclusive succeeded on a taken lock\n");
        return 1;
    }

    /* The virtual timer interrupt, 1s out, wakes the CPU if WFE sleeps */
    now = read_sysreg(cntvct_el0);
    write_sysreg(cntv_cval_el0, now + freq);
    write_sysreg(cntv_ctl_el0, 1);

    asm volatile("wfe");

    elapsed = read_sysreg(cntvct_el0) - now;
    write_sysreg(cntv_ctl_el0, 0);

    ml_printf("wfe after failed stxr: woke after %ld ticks\n", elapsed);
    if (elapsed > freq / 2) {
        ml_printf("FAIL: wfe missed the event of the failed stxr\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    uint64_t freq;
    int ret;

    ml_printf("WFxT Test\n");

    mmio_write32(GICD_BASE + GICD_ISENABLER0, 1u << VTIMER_PPI);
    mmio_write32(GICD_BASE + GICD_CTLR, 1);
    mmio_write32(GICC_BASE + GICC_PMR, 0xff);
    mmio_write32(GICC_BASE + GICC_CTLR, 1);

    /* Put the virtual count ahead of the physical count */
    write_sysreg(cntvoff_el2, -(1ULL << 60));
    asm volatile("isb");
    freq = read_sysreg(cntfrq_el0);

    ml_printf("cntvoff_el2=%lx cntfrq_el0=%ld\n",
              read_sysreg(cntvoff_el2), freq);

    ret = test_one("wfit", wfit, freq);
    ret |= test_one("wfet", wfet, freq);
    ret |= test_wfe_lock(freq);

    return ret;
}
