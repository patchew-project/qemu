/*
 * QEMU CXL Host Bridge Emulation
 *
 * Copyright (C) 2025, Phytium Technology Co, Ltd. All rights reserved.
 *
 * Based on gpex.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci-host/cxl_host_bridge.h"

static void cxl_host_set_irq(void *opaque, int irq_num, int level)
{
    CXLHostBridge *host = opaque;

    qemu_set_irq(host->irq[irq_num], level);
}

int cxl_host_set_irq_num(CXLHostBridge *host, int index, int gsi)
{
    if (index >= PCI_NUM_PINS) {
        return -EINVAL;
    }

    host->irq_num[index] = gsi;
    return 0;
}

static PCIINTxRoute cxl_host_route_intx_pin_to_irq(void *opaque, int pin)
{
    CXLHostBridge *host = opaque;
    int gsi = host->irq_num[pin];
    PCIINTxRoute route = {
       .irq = gsi,
       .mode = gsi < 0 ? PCI_INTX_DISABLED : PCI_INTX_ENABLED,
    };

    return route;
}

static const char *cxl_host_root_bus_path(PCIHostState *host_bridge,
                                          PCIBus *rootbus)
{
    return "0001:00";
}

void cxl_host_hook_up_registers(CXLState *cxl_state, CXLHostBridge *host)
{
    CXLComponentState *cxl_cstate = &host->cxl_cstate;
    struct MemoryRegion *mr = &cxl_cstate->crb.component_registers;

    memory_region_add_subregion(&cxl_state->host_mr, 0, mr);
}

static void cxl_host_reset(CXLHostBridge *host)
{
    CXLComponentState *cxl_cstate = &host->cxl_cstate;
    uint32_t *reg_state = cxl_cstate->crb.cache_mem_registers;
    uint32_t *write_msk = cxl_cstate->crb.cache_mem_regs_write_mask;

    cxl_component_register_init_common(reg_state, write_msk, CXL2_RC);

    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, TARGET_COUNT,
                     8);
}

static void cxl_host_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    CXLHostBridge *host = CXL_HOST(dev);
    CXLComponentState *cxl_cstate = &host->cxl_cstate;
    struct MemoryRegion *mr = &cxl_cstate->crb.component_registers;
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    PCIExpressHost *pex = PCIE_HOST_BRIDGE(dev);
    int i;

    cxl_host_reset(host);
    cxl_component_register_block_init(OBJECT(dev), cxl_cstate, TYPE_CXL_HOST);
    sysbus_init_mmio(sbd, mr);

    pcie_host_mmcfg_init(pex, PCIE_MMCFG_SIZE_MAX);
    sysbus_init_mmio(sbd, &pex->mmio);

    memory_region_init(&host->mmio, OBJECT(host), "cxl_host_mmio",
                       UINT64_MAX);

    memory_region_init_io(&host->mmio_window, OBJECT(host),
                          &unassigned_io_ops, OBJECT(host),
                          "cxl_host_mmio_window", UINT64_MAX);

    memory_region_add_subregion(&host->mmio_window, 0, &host->mmio);
    sysbus_init_mmio(sbd, &host->mmio_window);

    /* ioport window init, 64K is the legacy size in x86 */
    memory_region_init(&host->ioport, OBJECT(host), "cxl_host_ioport",
                       64 * 1024);

    memory_region_init_io(&host->ioport_window, OBJECT(host),
                          &unassigned_io_ops, OBJECT(host),
                          "cxl_host_ioport_window", 64 * 1024);

    memory_region_add_subregion(&host->ioport_window, 0, &host->ioport);
    sysbus_init_mmio(sbd, &host->ioport_window);

    /* PCIe host bridge use 4 legacy IRQ lines */
    for (i = 0; i < PCI_NUM_PINS; i++) {
        sysbus_init_irq(sbd, &host->irq[i]);
        host->irq_num[i] = -1;
    }

    pci->bus = pci_register_root_bus(dev, "cxlhost.0", cxl_host_set_irq,
                                     pci_swizzle_map_irq_fn, host, &host->mmio,
                                     &host->ioport, 0, 4, TYPE_CXL_BUS);
    pci->bus->flags |= PCI_BUS_CXL;

    pci_bus_set_route_irq_fn(pci->bus, cxl_host_route_intx_pin_to_irq);
}

static void cxl_host_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(class);

    hc->root_bus_path = cxl_host_root_bus_path;
    dc->realize = cxl_host_realize;
    dc->desc = "CXL Host Bridge";
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "cxl";
}

static const TypeInfo cxl_host_info = {
    .name          = TYPE_CXL_HOST,
    .parent        = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(CXLHostBridge),
    .class_init    = cxl_host_class_init,
};

static void cxl_host_register(void)
{
    type_register_static(&cxl_host_info);
}

type_init(cxl_host_register)
