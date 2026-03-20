/*
 * GICv3 Helper Library Implementation
 *
 * Copyright (c) 2024 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "gicv3.h"

#define write_sysreg(r, v) do {                     \
        uint64_t __val = (uint64_t)(v);             \
        asm volatile("msr " #r ", %x0"              \
                 : : "rZ" (__val));                 \
} while (0)

#define isb() asm volatile("isb" : : : "memory")

static inline void write_reg(uintptr_t addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t read_reg(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

void gicv3_init(void)
{
    uint32_t val;

    /* 1. Enable Distributor ARE and Group 1 NS */
    val = read_reg(GICD_CTLR);
    val |= GICD_CTLR_ARE_NS | GICD_CTLR_ENA_G1NS;
    write_reg(GICD_CTLR, val);

    /* 2. Wake up Redistributor 0 */
    /* Clear ProcessorSleep */
    val = read_reg(GICR_BASE + GICR_WAKER);
    val &= ~GICR_WAKER_ProcessorSleep;
    write_reg(GICR_BASE + GICR_WAKER, val);

    /* Wait for ChildrenAsleep to be cleared */
    while (read_reg(GICR_BASE + GICR_WAKER) & GICR_WAKER_ChildrenAsleep) {
        /* spin */
    }

    /* 3. Enable CPU interface */
    /* Set Priority Mask to allow all interrupts */
    write_sysreg(ICC_PMR_EL1, 0xff);
    /* Enable Group 1 Non-Secure interrupts */
    write_sysreg(ICC_IGRPEN1_EL1, 1);
    isb();
}

void gicv3_enable_irq(unsigned int irq)
{
    if (irq < 32) {
        /* PPI: use GICR_ISENABLER0 */
        uintptr_t addr;

        /* Set Group 1 */
        addr = GICR_BASE + GICR_IGROUPR0;
        write_reg(addr, read_reg(addr) | (1U << irq));

        /* Set priority (0xa0) */
        addr = GICR_BASE + GICR_IPRIORITYR0 + irq;
        *(volatile uint8_t *)addr = 0xa0;

        /* Enable it */
        addr = GICR_BASE + GICR_ISENABLER0;
        write_reg(addr, 1U << irq);
    } else {
        /* SPI: not implemented yet */
    }
}
