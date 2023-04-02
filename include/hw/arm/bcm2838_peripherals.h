/*
 * BCM2838 peripherals emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2838_PERIPHERALS_H
#define BCM2838_PERIPHERALS_H

#include "hw/sysbus.h"
#include "hw/or-irq.h"

#include "hw/arm/bcm2835_peripherals.h"
#include "hw/misc/bcm2838_rng200.h"
#include "hw/misc/bcm2838_thermal.h"
#include "hw/arm/bcm2838_pcie.h"
#include "hw/sd/sdhci.h"
#include "hw/gpio/bcm2838_gpio.h"


#define TYPE_BCM2838_PERIPHERALS "bcm2838-peripherals"
OBJECT_DECLARE_TYPE(BCM2838PeripheralState, BCM2838PeripheralClass,
                    BCM2838_PERIPHERALS)

struct BCM2838PeripheralState {
    /*< private >*/
    RaspiPeripheralBaseState parent_obj;

    /*< public >*/
    MemoryRegion peri_low_mr;
    MemoryRegion peri_low_mr_alias;
    MemoryRegion mphi_mr_alias;
    MemoryRegion pcie_mmio_alias;

    BCM2838Rng200State rng200;
    Bcm2838ThermalState thermal;
    SDHCIState emmc2;
    UnimplementedDeviceState clkisp;
    BCM2838PcieHostState pcie_host;
    BCM2838GpioState gpio;

    OrIRQState mmc_irq_orgate;
    OrIRQState dma_7_8_irq_orgate;
    OrIRQState dma_9_10_irq_orgate;
};

struct BCM2838PeripheralClass {
    /*< private >*/
    RaspiPeripheralBaseClass parent_class;
    /*< public >*/
    uint64_t peri_low_size; /* Peripheral lower range size */
};

#endif /* BCM2838_PERIPHERALS_H */
