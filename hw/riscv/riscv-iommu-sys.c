/*
 * QEMU emulation of an RISC-V IOMMU (Ziommu) - Platform Device
 *
 * Copyright (C) 2022-2023 Rivos Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
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
#include "hw/pci/pci_bus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qemu/osdep.h"
#include "qom/object.h"

#include "cpu_bits.h"
#include "riscv-iommu.h"
#include "riscv-iommu-bits.h"

/* RISC-V IOMMU System Platform Device Emulation */

struct RISCVIOMMUStateSys {
    SysBusDevice     parent;
    uint64_t         addr;
    qemu_irq         irqs[4];
    RISCVIOMMUState  iommu;
};

/* interrupt delivery callback */
static void riscv_iommu_sys_notify(RISCVIOMMUState *iommu, unsigned vector)
{
    RISCVIOMMUStateSys *s = container_of(iommu, RISCVIOMMUStateSys, iommu);

    if (vector < RISCV_IOMMU_INTR_COUNT && s->irqs[vector]) {
        qemu_irq_pulse(s->irqs[vector]);
    }
}

static void riscv_iommu_sys_realize(DeviceState *dev, Error **errp)
{
    RISCVIOMMUStateSys *s = RISCV_IOMMU_SYS(dev);
    RISCVIOMMUState *iommu = &s->iommu;
    PCIBus *pci_bus;
    uint64_t cap = iommu->cap;
    int i;

    /* Support WSI only */
    cap = set_field(cap, RISCV_IOMMU_CAP_IGS, RISCV_IOMMU_CAP_IGS_WSI);
    qdev_prop_set_uint64(dev, "capabilities", cap);

    if (!qdev_realize(DEVICE(iommu), NULL, errp)) {
        return;
    }

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &iommu->regs_mr);
    if (s->addr) {
        sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, s->addr);
    }

    for (i = 0; i < RISCV_IOMMU_INTR_COUNT; i++) {
        sysbus_init_irq(&s->parent, &s->irqs[i]);
    }

    iommu->notify = riscv_iommu_sys_notify;

    pci_bus = (PCIBus *) object_resolve_path_type("", TYPE_PCI_BUS, NULL);
    if (pci_bus) {
        riscv_iommu_pci_setup_iommu(iommu, pci_bus, errp);
    }
}

static void riscv_iommu_sys_init(Object *obj)
{
    RISCVIOMMUStateSys *s = RISCV_IOMMU_SYS(obj);
    RISCVIOMMUState *iommu = &s->iommu;

    object_initialize_child(obj, "iommu", iommu, TYPE_RISCV_IOMMU);
    qdev_alias_all_properties(DEVICE(iommu), obj);
}

static Property riscv_iommu_sys_properties[] = {
    DEFINE_PROP_UINT64("addr", RISCVIOMMUStateSys, addr, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_iommu_sys_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = riscv_iommu_sys_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, riscv_iommu_sys_properties);
}

static const TypeInfo riscv_iommu_sys = {
    .name          = TYPE_RISCV_IOMMU_SYS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .class_init    = riscv_iommu_sys_class_init,
    .instance_init = riscv_iommu_sys_init,
    .instance_size = sizeof(RISCVIOMMUStateSys),
};

static void riscv_iommu_register_sys(void)
{
    type_register_static(&riscv_iommu_sys);
}

type_init(riscv_iommu_register_sys)
