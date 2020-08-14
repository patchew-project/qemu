/*
 * Cadence SDHCI emulation
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CADENCE_SDHCI_H
#define CADENCE_SDHCI_H

#include "qemu/bitops.h"
#include "hw/sd/sdhci.h"

/* HRS - Host Register Set (specific to Cadence) */

#define SDHCI_CDNS_HRS00                0x00    /* general information */
#define SDHCI_CDNS_HRS00_SWR                BIT(0)
#define SDHCI_CDNS_HRS00_POR_VAL            0x00010000

#define SDHCI_CDNS_HRS04                0x10    /* PHY access port */
#define SDHCI_CDNS_HRS04_WR                 BIT(24)
#define SDHCI_CDNS_HRS04_RD                 BIT(25)
#define SDHCI_CDNS_HRS04_ACK                BIT(26)

#define SDHCI_CDNS_HRS06                0x18    /* eMMC control */
#define SDHCI_CDNS_HRS06_TUNE_UP            BIT(15)

/* SRS - Slot Register Set (SDHCI-compatible) */
#define SDHCI_CDNS_SRS_BASE             0x200

#define CADENCE_SDHCI_CAPABILITIES 0x01E80080
#define CADENCE_SDHCI_REG_SIZE     0x100
#define CADENCE_SDHCI_NUM_REGS     (CADENCE_SDHCI_REG_SIZE / sizeof(uint32_t))

typedef struct CadenceSDHCIState {
    SysBusDevice parent;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t regs[CADENCE_SDHCI_NUM_REGS];

    SDHCIState slot;
} CadenceSDHCIState;

#define TYPE_CADENCE_SDHCI  "cadence.sdhci"
#define CADENCE_SDHCI(obj)  OBJECT_CHECK(CadenceSDHCIState, (obj), \
                                         TYPE_CADENCE_SDHCI)

#endif /* CADENCE_SDHCI_H */
