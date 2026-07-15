/*
 * QEMU Cavium Octeon PCI host bridge model.
 *
 * Copyright (c) 2026 Kirill A. Korinsky
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/resettable.h"
#include "hw/core/sysbus.h"
#include "hw/mips/octeon_internal.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "qemu/module.h"
#include "system/memory.h"

#define TYPE_OCTEON_PCI_HOST "octeon-pci-host"
OBJECT_DECLARE_TYPE(OcteonPCIHostState, OcteonPCIHostClass,
                    OCTEON_PCI_HOST)

#define OCTEON_PCIE_CFG_BASE        0x1190400000000ULL
#define OCTEON_PCIE_CFG_SIZE        0x20000000ULL
#define OCTEON_PCIE_SLI_CFG_BASE    0x1190c00000000ULL
#define OCTEON_PCIE_SLI_CFG_PORTS   4
#define OCTEON_PCIE_SLI_CFG_PORT_SIZE (1ULL << 32)
#define OCTEON_PCIE_SLI_CFG_SIZE    \
    (OCTEON_PCIE_SLI_CFG_PORTS * OCTEON_PCIE_SLI_CFG_PORT_SIZE)
#define OCTEON_PCIE0_IO_BASE        0x11a0400000000ULL
#define OCTEON_PCIE0_IO_SIZE        (1ULL << 32)
#define OCTEON_PCIE0_MEM_SIZE       (1ULL << 32)
#define OCTEON_PCIE0_ENDPOINT_DEVFN PCI_DEVFN(0x0b, 0)
#define OCTEON_PEMX_CFG_WR0         0xc0000028
#define OCTEON_PEMX_CFG_RD0         0xc0000030
#define OCTEON_PCIE_CFG006          0x018
#define OCTEON_PCIE_CFG031          0x07c
#define OCTEON_PCIE_CFG032          0x080
#define OCTEON_PCIE_CFG068          0x110

struct OcteonPCIHostState {
    PCIHostState parent_obj;
    MemoryRegion mem;
    MemoryRegion io;
    uint32_t pem_cfg_rd_addr;
    uint64_t pem_cfg_wr;
    GHashTable *root_cfg;
};

struct OcteonPCIHostClass {
    PCIHostBridgeClass parent_class;
    ResettablePhases parent_phases;
};

static void octeon_pci_host_reset_hold(Object *obj, ResetType type)
{
    OcteonPCIHostState *host = OCTEON_PCI_HOST(obj);
    OcteonPCIHostClass *opc = OCTEON_PCI_HOST_GET_CLASS(obj);

    if (opc->parent_phases.hold) {
        opc->parent_phases.hold(obj, type);
    }

    host->pem_cfg_rd_addr = 0;
    host->pem_cfg_wr = 0;
    g_hash_table_remove_all(host->root_cfg);
}

static OcteonPCIHostState *octeon_pci_host(OcteonState *s)
{
    return OCTEON_PCI_HOST(s->pci_host);
}

static uint32_t octeon_pcie_root_cfg_default(uint32_t reg)
{
    switch (reg) {
    case OCTEON_PCIE_CFG006:
        return 0x00010101;
    case OCTEON_PCIE_CFG031:
        return 0x00000011;
    case OCTEON_PCIE_CFG032:
        return 0x20110000;
    default:
        return 0;
    }
}

static uint32_t octeon_pcie_root_cfg_read(OcteonPCIHostState *host,
                                           uint32_t reg)
{
    uint64_t value;

    reg &= ~3U;
    switch (reg) {
    case OCTEON_PCIE_CFG006:
    case OCTEON_PCIE_CFG031:
    case OCTEON_PCIE_CFG032:
        return octeon_pcie_root_cfg_default(reg);
    default:
        break;
    }

    if (octeon_reg_lookup(host->root_cfg, reg, &value)) {
        return value;
    }

    return octeon_pcie_root_cfg_default(reg);
}

static void octeon_pcie_root_cfg_write(OcteonPCIHostState *host,
                                        uint32_t reg, uint32_t value)
{
    reg &= ~3U;
    if (reg == OCTEON_PCIE_CFG068) {
        value = 0;
    }

    octeon_reg_store(host->root_cfg, reg, value);
}

uint64_t octeon_pci_pem_read(OcteonState *s, hwaddr reg,
                             hwaddr addr, unsigned size)
{
    OcteonPCIHostState *host = octeon_pci_host(s);
    uint64_t value;

    switch (reg) {
    case OCTEON_PEMX_CFG_RD0:
        value = ((uint64_t)octeon_pcie_root_cfg_read(
                     host, host->pem_cfg_rd_addr) << 32) |
                host->pem_cfg_rd_addr;
        break;
    case OCTEON_PEMX_CFG_WR0:
        value = host->pem_cfg_wr;
        break;
    default:
        g_assert_not_reached();
    }

    return octeon_read64(value, addr, size);
}

void octeon_pci_pem_write(OcteonState *s, hwaddr reg, hwaddr addr,
                          uint64_t value, unsigned size)
{
    OcteonPCIHostState *host = octeon_pci_host(s);

    switch (reg) {
    case OCTEON_PEMX_CFG_RD0:
        value = octeon_write64(0, addr, value, size);
        host->pem_cfg_rd_addr = value;
        break;
    case OCTEON_PEMX_CFG_WR0:
        value = octeon_write64(host->pem_cfg_wr, addr, value, size);
        octeon_pcie_root_cfg_write(host, value, value >> 32);
        host->pem_cfg_wr = value;
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t octeon_pci_absent(unsigned size)
{
    return size == 1 ? 0xff : size == 2 ? 0xffff : 0xffffffffU;
}

static bool octeon_pcie_config_addr(hwaddr addr, uint32_t *pci_addr,
                                    bool sli_cfg)
{
    unsigned int bus = (addr >> 20) & 0xff;
    unsigned int dev = (addr >> 15) & 0x1f;
    unsigned int fn = (addr >> 12) & 0x7;
    unsigned int reg = addr & 0xfff;

    if (sli_cfg) {
        if (addr >= OCTEON_PCIE_SLI_CFG_PORT_SIZE ||
            bus != 0 || dev != 0 || fn != 0 ||
            reg >= PCI_CONFIG_SPACE_SIZE) {
            return false;
        }
    } else if (bus != 1 || dev != 0 || fn != 0 ||
               reg >= PCI_CONFIG_SPACE_SIZE) {
        return false;
    }

    *pci_addr = 0x80000000U | (OCTEON_PCIE0_ENDPOINT_DEVFN << 8) | reg;
    return true;
}

static uint64_t octeon_pcie_cfg_read(void *opaque, hwaddr addr,
                                     unsigned size)
{
    OcteonState *s = opaque;
    uint32_t pci_addr;

    if (!octeon_pcie_config_addr(addr, &pci_addr, false)) {
        return octeon_pci_absent(size);
    }

    return pci_data_read(s->pci_bus, pci_addr, size);
}

static void octeon_pcie_cfg_write(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned size)
{
    OcteonState *s = opaque;
    uint32_t pci_addr;

    if (octeon_pcie_config_addr(addr, &pci_addr, false)) {
        pci_data_write(s->pci_bus, pci_addr, value, size);
    }
}

static const MemoryRegionOps octeon_pcie_cfg_ops = {
    .read = octeon_pcie_cfg_read,
    .write = octeon_pcie_cfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t octeon_pcie_sli_cfg_read(void *opaque, hwaddr addr,
                                         unsigned size)
{
    OcteonState *s = opaque;
    uint32_t pci_addr;

    if (!octeon_pcie_config_addr(addr, &pci_addr, true)) {
        return octeon_pci_absent(size);
    }

    return pci_data_read(s->pci_bus, pci_addr, size);
}

static void octeon_pcie_sli_cfg_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned size)
{
    OcteonState *s = opaque;
    uint32_t pci_addr;

    if (octeon_pcie_config_addr(addr, &pci_addr, true)) {
        pci_data_write(s->pci_bus, pci_addr, value, size);
    }
}

static const MemoryRegionOps octeon_pcie_sli_cfg_ops = {
    .read = octeon_pcie_sli_cfg_read,
    .write = octeon_pcie_sli_cfg_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static int octeon_pci_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return 0;
}

static void octeon_pci_set_irq(void *opaque, int irq_num, int level)
{
    octeon_irq_set(opaque, OCTEON_IRQ_PCI, level);
}

static void octeon_pci_host_init(Object *obj)
{
    OcteonPCIHostState *host = OCTEON_PCI_HOST(obj);

    host->root_cfg = g_hash_table_new_full(octeon_uint64_hash,
                                           octeon_uint64_equal,
                                           g_free, g_free);
}

static void octeon_pci_host_finalize(Object *obj)
{
    OcteonPCIHostState *host = OCTEON_PCI_HOST(obj);

    g_hash_table_destroy(host->root_cfg);
}

static void octeon_pci_host_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    OcteonPCIHostClass *opc = OCTEON_PCI_HOST_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->fw_name = "pci";
    dc->user_creatable = false;
    resettable_class_set_parent_phases(rc, NULL,
                                       octeon_pci_host_reset_hold, NULL,
                                       &opc->parent_phases);
}

static const TypeInfo octeon_pci_host_info = {
    .name          = TYPE_OCTEON_PCI_HOST,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(OcteonPCIHostState),
    .instance_init = octeon_pci_host_init,
    .instance_finalize = octeon_pci_host_finalize,
    .class_size    = sizeof(OcteonPCIHostClass),
    .class_init    = octeon_pci_host_class_init,
};

static void octeon_pci_host_register_types(void)
{
    type_register_static(&octeon_pci_host_info);
}

type_init(octeon_pci_host_register_types)

void octeon_init_pci(OcteonState *s)
{
    DeviceState *dev;
    OcteonPCIHostState *host;
    PCIHostState *phb;

    dev = qdev_new(TYPE_OCTEON_PCI_HOST);
    host = OCTEON_PCI_HOST(dev);
    s->pci_host = dev;
    phb = PCI_HOST_BRIDGE(dev);

    memory_region_init(&host->mem, OBJECT(dev), "octeon.pci-mem",
                       OCTEON_PCIE0_MEM_SIZE);
    memory_region_init(&host->io, OBJECT(dev), "octeon.pci-io",
                       OCTEON_PCIE0_IO_SIZE);

    phb->bus = pci_root_bus_new(dev, "pci", &host->mem, &host->io,
                                OCTEON_PCIE0_ENDPOINT_DEVFN, TYPE_PCI_BUS);
    s->pci_bus = phb->bus;
    pci_bus_irqs(s->pci_bus, octeon_pci_set_irq, s, 1);
    pci_bus_map_irqs(s->pci_bus, octeon_pci_map_irq);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    memory_region_init_io(&s->pcie_cfg, NULL, &octeon_pcie_cfg_ops, s,
                          "octeon.pcie-config", OCTEON_PCIE_CFG_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_PCIE_CFG_BASE,
                                &s->pcie_cfg);

    memory_region_init_io(&s->pcie_sli_cfg, NULL, &octeon_pcie_sli_cfg_ops,
                          s, "octeon.pcie-sli-config",
                          OCTEON_PCIE_SLI_CFG_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                OCTEON_PCIE_SLI_CFG_BASE,
                                &s->pcie_sli_cfg);

    memory_region_init_alias(&s->pcie0_io, NULL, "octeon.pcie0-io",
                             &host->io, 0,
                             OCTEON_PCIE0_IO_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_PCIE0_IO_BASE,
                                &s->pcie0_io);

}
