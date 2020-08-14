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

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/sd/cadence_sdhci.h"
#include "sdhci-internal.h"

#define TO_REG(addr)    ((addr) / sizeof(uint32_t))

static void cadence_sdhci_reset(DeviceState *dev)
{
    CadenceSDHCIState *sdhci = CADENCE_SDHCI(dev);

    memset(sdhci->regs, 0, CADENCE_SDHCI_REG_SIZE);
    sdhci->regs[TO_REG(SDHCI_CDNS_HRS00)] = SDHCI_CDNS_HRS00_POR_VAL;
}

static uint64_t cadence_sdhci_read(void *opaque, hwaddr addr, unsigned int size)
{
    uint32_t val = 0;
    CadenceSDHCIState *sdhci = opaque;

    if (addr < CADENCE_SDHCI_REG_SIZE) {
        val = sdhci->regs[TO_REG(addr)];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }

    return (uint64_t)val;
}

static void cadence_sdhci_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned int size)
{
    CadenceSDHCIState *sdhci = opaque;
    uint32_t val32 = (uint32_t)val;

    switch (addr) {
    case SDHCI_CDNS_HRS00:
        /*
         * The only writable bit is SWR (software reset) and it automatically
         * clears to zero, so essentially this register remains unchanged.
         */
        if (val32 & SDHCI_CDNS_HRS00_SWR) {
            cadence_sdhci_reset(DEVICE(sdhci));
            sdhci_poweron_reset(DEVICE(&sdhci->slot));
        }

        break;
    case SDHCI_CDNS_HRS04:
        /*
         * Only emulate the ACK bit behavior when read or write transaction
         * are requested.
         */
        if (val32 & (SDHCI_CDNS_HRS04_WR | SDHCI_CDNS_HRS04_RD)) {
            val32 |= SDHCI_CDNS_HRS04_ACK;
        } else {
            val32 &= ~SDHCI_CDNS_HRS04_ACK;
        }

        sdhci->regs[TO_REG(addr)] = val32;
        break;
    case SDHCI_CDNS_HRS06:
        if (val32 & SDHCI_CDNS_HRS06_TUNE_UP) {
            val32 &= ~SDHCI_CDNS_HRS06_TUNE_UP;
        }

        sdhci->regs[TO_REG(addr)] = val32;
        break;
    default:
        if (addr < CADENCE_SDHCI_REG_SIZE) {
            sdhci->regs[TO_REG(addr)] = val32;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Out-of-bounds write at 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
        }
    }
}

static const MemoryRegionOps cadence_sdhci_ops = {
    .read = cadence_sdhci_read,
    .write = cadence_sdhci_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void cadence_sdhci_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    CadenceSDHCIState *sdhci = CADENCE_SDHCI(dev);
    SysBusDevice *sbd_slot = SYS_BUS_DEVICE(&sdhci->slot);

    memory_region_init_io(&sdhci->iomem, OBJECT(sdhci), &cadence_sdhci_ops,
                          sdhci, TYPE_CADENCE_SDHCI, 0x1000);
    sysbus_init_mmio(sbd, &sdhci->iomem);

    sysbus_realize(sbd_slot, errp);
    memory_region_add_subregion(&sdhci->iomem, SDHCI_CDNS_SRS_BASE,
                                &sdhci->slot.iomem);
}

static const VMStateDescription vmstate_cadence_sdhci = {
    .name = TYPE_CADENCE_SDHCI,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, CadenceSDHCIState, CADENCE_SDHCI_NUM_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static void cadence_sdhci_class_init(ObjectClass *classp, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(classp);

    dc->realize = cadence_sdhci_realize;
    dc->reset = cadence_sdhci_reset;
    dc->vmsd = &vmstate_cadence_sdhci;
}

static TypeInfo cadence_sdhci_info = {
    .name          = TYPE_CADENCE_SDHCI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CadenceSDHCIState),
    .class_init    = cadence_sdhci_class_init,
};

static void cadence_sdhci_register_types(void)
{
    type_register_static(&cadence_sdhci_info);
}

type_init(cadence_sdhci_register_types)
