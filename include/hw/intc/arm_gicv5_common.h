/*
 * Common base class for GICv5 IRS
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_ARM_GICV5_COMMON_H
#define HW_INTC_ARM_GICV5_COMMON_H

#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "hw/intc/arm_gicv5_types.h"
#include "target/arm/cpu-qom.h"

/*
 * QEMU interface:
 *
 * + QOM array property "cpus": CPUState pointers to each CPU
 *   connected to this IRS.
 * + QOM array property "cpu-iaffids": array of uint32_t giving the
 *   IAFFID for each CPU in the "cpus" property array
 * + QOM property "irsid": unique identifier for this IRS in the system
 *   (this is IRS_IDR0.IRSID); default is 0
 * + QOM property "spi-range": total number of SPIs in the system
 *   IRS (this is IRS_IDR5.SPI_RANGE); must be set
 * + QOM property "spi-base": minimum SPI INTID.ID implemented on this
 *   IRS (this is IRS_IDR7.SPI_BASE); default is 0
 * + QOM property "spi-irs-range": number of SPI INTID.ID managed on this
 *   IRS (this is IRS_IDR6.SPI_IRS_RANGE); defaults to value of spi-range
 *
 * sysbus MMIO regions (in order matching IRS_IDR0.INT_DOM encoding):
 * - IRS config frame for the Secure Interrupt Domain
 * - IRS config frame for the Non-secure Interrupt Domain
 * - IRS config frame for the EL3 Interrupt Domain
 * - IRS config frame for the Realm Interrupt Domain
 *
 * Note that even if this particular IRS does not implement all four
 * interrupt domains it will still expose four sysbus MMIO regions.
 * The regions corresponding to unimplemented domains will always
 * fail accesses with a decode error. Generally the SoC/board should
 * probably not map a region for a domain that it configured the IRS
 * to not implement; the regions are only exposed so that changing
 * which domains are implemented doesn't reorder which sysbus MMIO
 * region is which (e.g. NS will always be 1 and EL3 will always be 2).
 */

#define TYPE_ARM_GICV5_COMMON "arm-gicv5-common"

OBJECT_DECLARE_TYPE(GICv5Common, GICv5CommonClass, ARM_GICV5_COMMON)

/*
 * This class is for common state that will eventually be shared
 * between TCG and KVM implementations of the GICv5.
 */
struct GICv5Common {
    SysBusDevice parent_obj;

    MemoryRegion iomem[NUM_GICV5_DOMAINS];

    /* Bits here are set for each physical interrupt domain implemented */
    uint8_t implemented_domains;

    /* Properties */
    uint32_t num_cpus;
    ARMCPU **cpus;
    uint32_t num_cpu_iaffids;
    uint32_t *cpu_iaffids;

    uint32_t irsid;
    uint32_t spi_base;
    uint32_t spi_irs_range;
    uint32_t spi_range;
};

struct GICv5CommonClass {
    SysBusDeviceClass parent_class;
};


#define IRS_CONFIG_FRAME_SIZE 0x10000

/**
 * gicv5_common_init_irqs_and_mmio: Create IRQs and MMIO regions for the GICv5
 * @s: GIC object
 * @ops: array of MemoryRegionOps that implement the config frames behaviour
 *
 * Subclasses of ARM_GICV5_COMMON should call this to create the sysbus
 * MemoryRegions for the IRS config frames, passing in a four element array
 * of MemoryRegionOps structs.
 */
void gicv5_common_init_irqs_and_mmio(GICv5Common *cs,
                                     const MemoryRegionOps ops[NUM_GICV5_DOMAINS]);

/**
 * gicv5_domain_implemented: Return true if this IRS implements this domain
 * @s: GIC object
 * @domain: domain to check
 */
static inline bool gicv5_domain_implemented(GICv5Common *cs, GICv5Domain domain)
{
    return cs->implemented_domains & (1 << domain);
}

#endif
