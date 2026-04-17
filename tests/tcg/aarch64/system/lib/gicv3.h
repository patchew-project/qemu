/*
 * GICv3 Helper Library
 *
 * Copyright (c) 2026 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef GICV3_H
#define GICV3_H

/*
 * This duplicates just the bits of hw/intc/gicv3_internal.h we
 * need for basic functionality.
 */

/* Virt machine GICv3 base addresses */
#define GICD_BASE       0x08000000  /* c.f. VIRT_GIC_DIST */
#define GICR_BASE       0x080a0000  /* c.f. VIRT_GIC_REDIST */

/* Distributor registers */
#define GICD_CTLR       (GICD_BASE + 0x0000)
#define GICD_TYPER      (GICD_BASE + 0x0004)
#define GICD_IIDR       (GICD_BASE + 0x0008)

/* Redistributor registers (per-CPU) */
#define GICR_SGI_OFFSET 0x00010000

#define GICR_CTLR       0x0000
#define GICR_WAKER      0x0014

#define GICR_IGROUPR0   (GICR_SGI_OFFSET + 0x0080)
#define GICR_ISENABLER0 (GICR_SGI_OFFSET + 0x0100)
#define GICR_IPRIORITYR (GICR_SGI_OFFSET + 0x0400)

/* GICD_CTLR fields */
#define GICD_CTLR_ENA_GRP0   (1U << 0)
#define GICD_CTLR_ENA_GRP1NS (1U << 1)
#define GICD_CTLR_ARE_NS     (1U << 5)

/* GICR_WAKER bits */
#define GICR_WAKER_ProcessorSleep (1U << 1)
#define GICR_WAKER_ChildrenAsleep (1U << 2)

/**
 * gicv3_init:
 *
 * Initialize GICv3 distributor and the redistributor for the current CPU.
 */
void gicv3_init(void);

/**
 * gicv3_enable_irq:
 * @irq: The IRQ number to enable
 *
 * Enable the specified IRQ (SPI or PPI).
 */
void gicv3_enable_irq(unsigned int irq);

#endif /* GICV3_H */
