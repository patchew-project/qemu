/*
 * BCM2838 PCIe Root Complex emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/pci-host/gpex.h"
#include "hw/qdev-properties.h"
#include "hw/arm/bcm2838_pcie.h"
#include "trace.h"

static uint32_t bcm2838_pcie_config_read(PCIDevice *d,
                                         uint32_t address, int len)
{
    return pci_default_read_config(d, address, len);
}

static void bcm2838_pcie_config_write(PCIDevice *d, uint32_t addr, uint32_t val,
                                      int len)
{
    return pci_default_write_config(d, addr, val, len);
}

static uint64_t bcm2838_pcie_host_read(void *opaque, hwaddr offset,
                                       unsigned size) {
    hwaddr mmcfg_addr;
    uint64_t value = ~0;
    BCM2838PcieHostState *s = opaque;
    PCIExpressHost *pcie_hb = PCIE_HOST_BRIDGE(s);
    uint8_t *root_regs = s->root_port.regs;
    uint32_t *cfg_idx = (uint32_t *)(root_regs + BCM2838_PCIE_EXT_CFG_INDEX
                                     - PCIE_CONFIG_SPACE_SIZE);

    if (offset - PCIE_CONFIG_SPACE_SIZE + size <= sizeof(s->root_port.regs)) {
        switch (offset) {
        case BCM2838_PCIE_EXT_CFG_DATA
            ... BCM2838_PCIE_EXT_CFG_DATA + PCIE_CONFIG_SPACE_SIZE - 1:
            mmcfg_addr = *cfg_idx
                | PCIE_MMCFG_CONFOFFSET(offset - BCM2838_PCIE_EXT_CFG_DATA);
            value = pcie_hb->mmio.ops->read(opaque, mmcfg_addr, size);
            break;
        default:
            memcpy(&value, root_regs + offset - PCIE_CONFIG_SPACE_SIZE, size);
        }
    } else {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: out-of-range access, %u bytes @ offset 0x%04" PRIx64 "\n",
            __func__, size, offset);
    }

    trace_bcm2838_pcie_host_read(size, offset, value);
    return value;
}

static void bcm2838_pcie_host_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size) {
    hwaddr mmcfg_addr;
    BCM2838PcieHostState *s = opaque;
    PCIExpressHost *pcie_hb = PCIE_HOST_BRIDGE(s);
    uint8_t *root_regs = s->root_port.regs;
    uint32_t *cfg_idx = (uint32_t *)(root_regs + BCM2838_PCIE_EXT_CFG_INDEX
                                     - PCIE_CONFIG_SPACE_SIZE);

    trace_bcm2838_pcie_host_write(size, offset, value);

    if (offset - PCIE_CONFIG_SPACE_SIZE + size <= sizeof(s->root_port.regs)) {
        switch (offset) {
        case BCM2838_PCIE_EXT_CFG_DATA
            ... BCM2838_PCIE_EXT_CFG_DATA + PCIE_CONFIG_SPACE_SIZE - 1:
            mmcfg_addr = *cfg_idx
                | PCIE_MMCFG_CONFOFFSET(offset - BCM2838_PCIE_EXT_CFG_DATA);
            pcie_hb->mmio.ops->write(opaque, mmcfg_addr, value, size);
            break;
        default:
            memcpy(root_regs + offset - PCIE_CONFIG_SPACE_SIZE, &value, size);
        }
    } else {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: out-of-range access, %u bytes @ offset 0x%04" PRIx64 "\n",
            __func__, size, offset);
    }
}

static const MemoryRegionOps bcm2838_pcie_host_ops = {
    .read = bcm2838_pcie_host_read,
    .write = bcm2838_pcie_host_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {.max_access_size = sizeof(uint64_t)},
};

int bcm2838_pcie_host_set_irq_num(BCM2838PcieHostState *s, int index, int spi)
{
    if (index >= BCM2838_PCIE_NUM_IRQS) {
        return -EINVAL;
    }

    s->irq_num[index] = spi;
    return 0;
}

static void bcm2838_pcie_host_set_irq(void *opaque, int irq_num, int level)
{
    BCM2838PcieHostState *s = opaque;

    qemu_set_irq(s->irq[irq_num], level);
}

static PCIINTxRoute bcm2838_pcie_host_route_intx_pin_to_irq(void *opaque,
                                                            int pin)
{
    PCIINTxRoute route;
    BCM2838PcieHostState *s = opaque;

    route.irq = s->irq_num[pin];
    route.mode = route.irq < 0 ? PCI_INTX_DISABLED : PCI_INTX_ENABLED;

    return route;
}

static int bcm2838_pcie_host_map_irq(PCIDevice *pci_dev, int pin)
{
    return pin;
}

static void bcm2838_pcie_host_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    BCM2838PcieHostState *s = BCM2838_PCIE_HOST(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    int i;

    memory_region_init_io(&s->cfg_regs, OBJECT(s), &bcm2838_pcie_host_ops, s,
                          "bcm2838_pcie_cfg_regs", BCM2838_PCIE_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->cfg_regs);

    /*
     * The MemoryRegions io_mmio and io_ioport that we pass to
     * pci_register_root_bus() are not the same as the MemoryRegions
     * io_mmio_window and io_ioport_window that we expose as SysBus MRs.
     * The difference is in the behavior of accesses to addresses where no PCI
     * device has been mapped.
     *
     * io_mmio and io_ioport are the underlying PCI view of the PCI address
     * space, and when a PCI device does a bus master access to a bad address
     * this is reported back to it as a transaction failure.
     *
     * io_mmio_window and io_ioport_window implement "unmapped addresses read as
     * -1 and ignore writes"; this is a traditional x86 PC behavior, which is
     * not mandated properly by the PCI spec but expected by the majority of
     * PCI-using guest software, including Linux.
     *
     * We implement it in the PCIe host controller, by providing the *_window
     * MRs, which are containers with io ops that implement the 'background'
     * behavior and which hold the real PCI MRs as sub-regions.
     */
    memory_region_init(&s->io_mmio, OBJECT(s), "bcm2838_pcie_mmio", UINT64_MAX);
    memory_region_init(&s->io_ioport, OBJECT(s), "bcm2838_pcie_ioport",
                       64 * 1024);

    memory_region_init_io(&s->io_mmio_window, OBJECT(s),
                            &unassigned_io_ops, OBJECT(s),
                            "bcm2838_pcie_mmio_window", UINT64_MAX);
    memory_region_init_io(&s->io_ioport_window, OBJECT(s),
                            &unassigned_io_ops, OBJECT(s),
                            "bcm2838_pcie_ioport_window", 64 * 1024);

    memory_region_add_subregion(&s->io_mmio_window, 0, &s->io_mmio);
    memory_region_add_subregion(&s->io_ioport_window, 0, &s->io_ioport);
    sysbus_init_mmio(sbd, &s->io_mmio_window);
    sysbus_init_mmio(sbd, &s->io_ioport_window);

    for (i = 0; i < BCM2838_PCIE_NUM_IRQS; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
        s->irq_num[i] = -1;
    }

    pci->bus = pci_register_root_bus(dev, "pcie.0", bcm2838_pcie_host_set_irq,
                                     bcm2838_pcie_host_map_irq, s, &s->io_mmio,
                                     &s->io_ioport, 0, BCM2838_PCIE_NUM_IRQS,
                                     TYPE_PCIE_BUS);
    pci_bus_set_route_irq_fn(pci->bus, bcm2838_pcie_host_route_intx_pin_to_irq);
    qdev_realize(DEVICE(&s->root_port), BUS(pci->bus), &error_fatal);
}

static const char *bcm2838_pcie_host_root_bus_path(PCIHostState *host_bridge,
                                                   PCIBus *rootbus)
{
    return "0000:00";
}

static void bcm2838_pcie_host_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(class);

    hc->root_bus_path = bcm2838_pcie_host_root_bus_path;
    dc->realize = bcm2838_pcie_host_realize;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "pci";
}

static void bcm2838_pcie_host_initfn(Object *obj)
{
    BCM2838PcieHostState *s = BCM2838_PCIE_HOST(obj);
    BCM2838PcieRootState *root = &s->root_port;

    object_initialize_child(obj, "root_port", root, TYPE_BCM2838_PCIE_ROOT);
    qdev_prop_set_int32(DEVICE(root), "addr", PCI_DEVFN(0, 0));
    qdev_prop_set_bit(DEVICE(root), "multifunction", false);
}

static const TypeInfo bcm2838_pcie_host_info = {
    .name       = TYPE_BCM2838_PCIE_HOST,
    .parent     = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(BCM2838PcieHostState),
    .instance_init = bcm2838_pcie_host_initfn,
    .class_init = bcm2838_pcie_host_class_init,
};

/*
 * RC root part (D0:F0)
 */

static void bcm2838_pcie_root_port_reset_hold(Object *obj)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(obj);
    PCIDevice *dev = PCI_DEVICE(obj);
    BCM2838PcieRootState *s = BCM2838_PCIE_ROOT(dev);

    if (rpc->parent_phases.hold) {
        rpc->parent_phases.hold(obj);
    }

    memset(s->regs, 0xFF, sizeof(s->regs));
}

static void bcm2838_pcie_root_init(Object *obj)
{
    PCIBridge *br = PCI_BRIDGE(obj);
    br->bus_name = "pcie.1";
}

static void bcm2838_pcie_root_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(class);

    dc->desc = "BCM2711 PCIe Bridge";
    /*
     * PCI-facing part of the host bridge, not usable without the host-facing
     * part, which can't be device_add'ed.
     */

    resettable_class_set_parent_phases(rc, NULL,
                                       bcm2838_pcie_root_port_reset_hold,
                                       NULL, &rpc->parent_phases);

    dc->user_creatable = false;
    k->vendor_id = BCM2838_PCIE_VENDOR_ID;
    k->device_id = BCM2838_PCIE_DEVICE_ID;
    k->revision = BCM2838_PCIE_REVISION;

    k->config_read = bcm2838_pcie_config_read;
    k->config_write = bcm2838_pcie_config_write;

    rpc->exp_offset = BCM2838_PCIE_EXP_CAP_OFFSET;
    rpc->aer_offset = BCM2838_PCIE_AER_CAP_OFFSET;
}

static const TypeInfo bcm2838_pcie_root_info = {
    .name = TYPE_BCM2838_PCIE_ROOT,
    .parent = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(BCM2838PcieRootState),
    .instance_init = bcm2838_pcie_root_init,
    .class_init = bcm2838_pcie_root_class_init,
};

static void bcm2838_pcie_register(void)
{
    type_register_static(&bcm2838_pcie_root_info);
    type_register_static(&bcm2838_pcie_host_info);
}

type_init(bcm2838_pcie_register)
