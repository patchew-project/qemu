/*
 * BCM2838 peripherals emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/raspi_platform.h"
#include "hw/arm/bcm2838_peripherals.h"

#define PCIE_RC_OFFSET          0x1500000
#define PCIE_MMIO_OFFSET        0xc0000000
#define PCIE_MMIO_ARM_OFFSET    0x600000000
#define PCIE_MMIO_SIZE          0x40000000

/* Lower peripheral base address on the VC (GPU) system bus */
#define BCM2838_VC_PERI_LOW_BASE 0x7c000000

/* Capabilities for SD controller: no DMA, high-speed, default clocks etc. */
#define BCM2835_SDHC_CAPAREG 0x52134b4

static void bcm2838_peripherals_init(Object *obj)
{
    BCM2838PeripheralState *s = BCM2838_PERIPHERALS(obj);
    BCM2838PeripheralClass *bc = BCM2838_PERIPHERALS_GET_CLASS(obj);
    BCMSocPeripheralBaseState *s_base = BCM_SOC_PERIPHERALS_BASE(obj);

    /* Lower memory region for peripheral devices (exported to the Soc) */
    memory_region_init(&s->peri_low_mr, obj, "bcm2838-peripherals",
                       bc->peri_low_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->peri_low_mr);

    /* Random Number Generator */
    object_initialize_child(obj, "rng200", &s->rng200, TYPE_BCM2838_RNG200);

    /* Thermal */
    object_initialize_child(obj, "thermal", &s->thermal, TYPE_BCM2838_THERMAL);

    /* PCIe Host Bridge */
    object_initialize_child(obj, "pcie-host", &s->pcie_host,
                            TYPE_BCM2838_PCIE_HOST);

    /* Extended Mass Media Controller 2 */
    object_initialize_child(obj, "emmc2", &s->emmc2, TYPE_SYSBUS_SDHCI);

    /* GPIO */
    object_initialize_child(obj, "gpio", &s->gpio, TYPE_BCM2838_GPIO);

    object_property_add_const_link(OBJECT(&s->gpio), "sdbus-sdhci",
                                   OBJECT(&s_base->sdhci.sdbus));
    object_property_add_const_link(OBJECT(&s->gpio), "sdbus-sdhost",
                                   OBJECT(&s_base->sdhost.sdbus));

    object_initialize_child(obj, "mmc_irq_orgate", &s->mmc_irq_orgate,
                            TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&s->mmc_irq_orgate), "num-lines", 2,
                            &error_abort);

    object_initialize_child(obj, "dma_7_8_irq_orgate", &s->dma_7_8_irq_orgate,
                            TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&s->dma_7_8_irq_orgate), "num-lines", 2,
                            &error_abort);

    object_initialize_child(obj, "dma_9_10_irq_orgate", &s->dma_9_10_irq_orgate,
                            TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&s->dma_9_10_irq_orgate), "num-lines", 2,
                            &error_abort);
}

static void bcm2838_peripherals_realize(DeviceState *dev, Error **errp)
{
    DeviceState *mmc_irq_orgate;
    DeviceState *dma_7_8_irq_orgate;
    DeviceState *dma_9_10_irq_orgate;
    MemoryRegion *mphi_mr;
    BCM2838PeripheralState *s = BCM2838_PERIPHERALS(dev);
    BCMSocPeripheralBaseState *s_base = BCM_SOC_PERIPHERALS_BASE(dev);
    MemoryRegion *regs_mr;
    MemoryRegion *mmio_mr;
    MemoryRegion *rng200_mr;
    qemu_irq rng_200_irq;
    MemoryRegion *thermal_mr;

    int n;

    bcm_soc_peripherals_common_realize(dev, errp);

    /* Map lower peripherals into the GPU address space */
    memory_region_init_alias(&s->peri_low_mr_alias, OBJECT(s),
                             "bcm2838-peripherals", &s->peri_low_mr, 0,
                             memory_region_size(&s->peri_low_mr));
    memory_region_add_subregion_overlap(&s_base->gpu_bus_mr,
                                        BCM2838_VC_PERI_LOW_BASE,
                                        &s->peri_low_mr_alias, 1);

    /* Random Number Generator */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rng200), errp)) {
        return;
    }

    rng200_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rng200), 0);
    memory_region_add_subregion(&s_base->peri_mr, RNG_OFFSET, rng200_mr);

    rng_200_irq = qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                         BCM2835_IC_GPU_IRQ, INTERRUPT_RNG);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rng200), 0, rng_200_irq);

    /* THERMAL */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->thermal), errp)) {
        return;
    }
    thermal_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->thermal), 0);
    memory_region_add_subregion( &s->peri_low_mr, 0x15D2000, thermal_mr);

    /* Extended Mass Media Controller 2 */
    object_property_set_uint(OBJECT(&s->emmc2), "sd-spec-version", 3,
                             &error_abort);
    object_property_set_uint(OBJECT(&s->emmc2), "capareg",
                             BCM2835_SDHC_CAPAREG, &error_abort);
    object_property_set_bool(OBJECT(&s->emmc2), "pending-insert-quirk", true,
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->emmc2), errp)) {
        return;
    }

    memory_region_add_subregion(&s_base->peri_mr, EMMC2_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->emmc2),
                                0));

    /* According to DTS, EMMC and EMMC2 share one irq */
    if (!qdev_realize(DEVICE(&s->mmc_irq_orgate), NULL, errp)) {
        return;
    }

    mmc_irq_orgate = DEVICE(&s->mmc_irq_orgate);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->emmc2), 0,
                       qdev_get_gpio_in(mmc_irq_orgate, 0));

    sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->sdhci), 0,
                       qdev_get_gpio_in(mmc_irq_orgate, 1));

   /* Connect EMMC and EMMC2 to the interrupt controller */
    qdev_connect_gpio_out(mmc_irq_orgate, 0,
                          qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                                 BCM2835_IC_GPU_IRQ,
                                                 INTERRUPT_ARASANSDIO));

    /* Connect DMA 0-6 to the interrupt controller */
    for (n = 0; n < 7; n++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->dma), n,
                           qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                                  BCM2835_IC_GPU_IRQ,
                                                  GPU_INTERRUPT_DMA0 + n));
    }

   /* According to DTS, DMA 7 and 8 share one irq */
    if (!qdev_realize(DEVICE(&s->dma_7_8_irq_orgate), NULL, errp)) {
        return;
    }
    dma_7_8_irq_orgate = DEVICE(&s->dma_7_8_irq_orgate);

    /* Connect DMA 7-8 to the interrupt controller */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->dma), 7,
                       qdev_get_gpio_in(dma_7_8_irq_orgate, 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->dma), 8,
                       qdev_get_gpio_in(dma_7_8_irq_orgate, 1));

    qdev_connect_gpio_out(dma_7_8_irq_orgate, 0,
                          qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                                 BCM2835_IC_GPU_IRQ,
                                                 GPU_INTERRUPT_DMA7_8));

     /* According to DTS, DMA 9 and 10 share one irq */
    if (!qdev_realize(DEVICE(&s->dma_9_10_irq_orgate), NULL, errp)) {
        return;
    }
    dma_9_10_irq_orgate = DEVICE(&s->dma_9_10_irq_orgate);

   /* Connect DMA 9-10 to the interrupt controller */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->dma), 9,
                       qdev_get_gpio_in(dma_9_10_irq_orgate, 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->dma), 10,
                       qdev_get_gpio_in(dma_9_10_irq_orgate, 1));

    qdev_connect_gpio_out(dma_9_10_irq_orgate, 0,
                          qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                                 BCM2835_IC_GPU_IRQ,
                                                 GPU_INTERRUPT_DMA9_10));

    /* Connect DMA 11-14 to the interrupt controller */
    for (n = 11; n < 15; n++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->dma), n,
                           qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                                  BCM2835_IC_GPU_IRQ,
                                                  GPU_INTERRUPT_DMA11 + n
                                                  - 11));
    }

    /*
     * Connect DMA 15 to the interrupt controller, it is physically removed
     * from other DMA channels and exclusively used by the GPU
     */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->dma), 15,
                        qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                               BCM2835_IC_GPU_IRQ,
                                               GPU_INTERRUPT_DMA15));

    /* Map MPHI to BCM2838 memory map */
    mphi_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s_base->mphi), 0);
    memory_region_init_alias(&s->mphi_mr_alias, OBJECT(s), "mphi", mphi_mr, 0,
                             BCM2838_MPHI_SIZE);
    memory_region_add_subregion(&s_base->peri_mr, BCM2838_MPHI_OFFSET,
                                &s->mphi_mr_alias);

    /* PCIe Root Complex */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pcie_host), errp)) {
        return;
    }
    /* RC registers region */
    regs_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pcie_host), 0);
    memory_region_add_subregion(&s->peri_low_mr, PCIE_RC_OFFSET, regs_mr);
    /* MMIO region */
    mmio_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pcie_host), 1);
    memory_region_add_subregion(get_system_memory(), PCIE_MMIO_ARM_OFFSET,
                                mmio_mr);

    /* GPIO */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp)) {
        return;
    }
    memory_region_add_subregion(
        &s_base->peri_mr, GPIO_OFFSET,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gpio), 0));

    object_property_add_alias(OBJECT(s), "sd-bus", OBJECT(&s->gpio), "sd-bus");

    /* BCM2838 RPiVid ASB must be mapped to prevent kernel crash */
    create_unimp(s_base, &s->asb, "bcm2838-asb", BRDG_OFFSET, 0x24);
}

static void bcm2838_peripherals_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM2838PeripheralClass *bc = BCM2838_PERIPHERALS_CLASS(oc);
    BCMSocPeripheralBaseClass *bc_base = BCM_SOC_PERIPHERALS_BASE_CLASS(oc);

    bc->peri_low_size = 0x2000000;
    bc_base->peri_size = 0x1800000;
    dc->realize = bcm2838_peripherals_realize;
}

static const TypeInfo bcm2838_peripherals_type_info = {
    .name = TYPE_BCM2838_PERIPHERALS,
    .parent = TYPE_BCM_SOC_PERIPHERALS_BASE,
    .instance_size = sizeof(BCM2838PeripheralState),
    .instance_init = bcm2838_peripherals_init,
    .class_size = sizeof(BCM2838PeripheralClass),
    .class_init = bcm2838_peripherals_class_init,
};

static void bcm2838_peripherals_register_types(void)
{
    type_register_static(&bcm2838_peripherals_type_info);
}

type_init(bcm2838_peripherals_register_types)
