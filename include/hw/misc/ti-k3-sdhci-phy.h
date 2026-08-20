/*
 * TI K3 SDHCI PHY register-file stub (AM64x)
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_TI_K3_SDHCI_PHY_H
#define HW_MISC_TI_K3_SDHCI_PHY_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_K3_SDHCI_PHY "ti-k3-sdhci-phy"
OBJECT_DECLARE_SIMPLE_TYPE(TIK3SdhciPhyState, TI_K3_SDHCI_PHY)

#define TI_K3_SDHCI_PHY_SIZE 0x400

struct TIK3SdhciPhyState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t regs[TI_K3_SDHCI_PHY_SIZE / 4];
};

#endif
