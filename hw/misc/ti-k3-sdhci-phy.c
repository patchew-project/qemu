/*
 * TI K3 SDHCI PHY register-file stub (AM64x)
 *
 * RAM-backed SDHCI companion PHY window. Drive and delay registers are stored
 * as written. PHY_STAT1 reports CALDONE (bit 1) and DLLRDY (bit 0) set, so
 * calibration resp. DLL lock complete immediately.
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/ti-k3-sdhci-phy.h"

/* Read-side OR masks: {offset, bits}. */
static const struct {
    hwaddr offset;
    uint32_t bits;
} sdhci_phy_status_bits[] = {
    /* PHY_STAT1: CALDONE (bit 1) | DLLRDY (bit 0) */
    { 0x130, 0x3 },
};

static uint64_t ti_k3_sdhci_phy_read(void *opaque, hwaddr addr, unsigned size)
{
    TIK3SdhciPhyState *s = TI_K3_SDHCI_PHY(opaque);
    uint32_t val = s->regs[addr >> 2];

    for (size_t i = 0; i < ARRAY_SIZE(sdhci_phy_status_bits); i++) {
        if (addr == sdhci_phy_status_bits[i].offset) {
            val |= sdhci_phy_status_bits[i].bits;
        }
    }
    return val;
}

static void ti_k3_sdhci_phy_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    TIK3SdhciPhyState *s = TI_K3_SDHCI_PHY(opaque);

    s->regs[addr >> 2] = val;
}

static const MemoryRegionOps ti_k3_sdhci_phy_ops = {
    .read = ti_k3_sdhci_phy_read,
    .write = ti_k3_sdhci_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void ti_k3_sdhci_phy_reset(DeviceState *dev)
{
    TIK3SdhciPhyState *s = TI_K3_SDHCI_PHY(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void ti_k3_sdhci_phy_init(Object *obj)
{
    TIK3SdhciPhyState *s = TI_K3_SDHCI_PHY(obj);

    memory_region_init_io(&s->iomem, obj, &ti_k3_sdhci_phy_ops, s,
                          TYPE_TI_K3_SDHCI_PHY, TI_K3_SDHCI_PHY_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void ti_k3_sdhci_phy_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, ti_k3_sdhci_phy_reset);
}

static const TypeInfo ti_k3_sdhci_phy_info = {
    .name = TYPE_TI_K3_SDHCI_PHY,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TIK3SdhciPhyState),
    .instance_init = ti_k3_sdhci_phy_init,
    .class_init = ti_k3_sdhci_phy_class_init,
};

static void ti_k3_sdhci_phy_register_types(void)
{
    type_register_static(&ti_k3_sdhci_phy_info);
}

type_init(ti_k3_sdhci_phy_register_types)
