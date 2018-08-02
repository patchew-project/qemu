/*
 * PCI Expander Bridge Device Emulation
 *
 * Copyright (C) 2015 Red Hat Inc
 *
 * Authors:
 *   Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci-host/q35.h"
#include "hw/pci-bridge/pci_expander_bridge.h"
#include "qemu/range.h"
#include "qemu/error-report.h"
#include "sysemu/numa.h"
#include "qapi/visitor.h"
#include "qemu/units.h"

#define TYPE_PXB_BUS "pxb-bus"
#define PXB_BUS(obj) OBJECT_CHECK(PXBBus, (obj), TYPE_PXB_BUS)

#define TYPE_PXB_PCIE_BUS "pxb-pcie-bus"
#define PXB_PCIE_BUS(obj) OBJECT_CHECK(PXBBus, (obj), TYPE_PXB_PCIE_BUS)

typedef struct PXBBus {
    /*< private >*/
    PCIBus parent_obj;
    /*< public >*/

    char bus_path[8];
} PXBBus;

#define TYPE_PXB_DEVICE "pxb"
#define PXB_DEV(obj) OBJECT_CHECK(PXBDev, (obj), TYPE_PXB_DEVICE)

#define TYPE_PXB_PCIE_DEVICE "pxb-pcie"
#define PXB_PCIE_DEV(obj) OBJECT_CHECK(PXBDev, (obj), TYPE_PXB_PCIE_DEVICE)

#define PROP_PXB_PCIE_MAX_BUS "max_bus"
#define PROP_PXB_NUMA_NODE "numa_node"

typedef struct PXBPCIEHost PXBPCIEHost;
typedef struct PXBDev PXBDev;

typedef struct PXBDev {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    /* backlink to PXBPCIEHost, this makes it easier to get
     * mcfg properties in pxb-pcie-host bridge */
    PXBPCIEHost *pxbhost;

    uint32_t domain_nr; /* PCI domain number, non-zero means separate domain */
    uint8_t max_bus;    /* max bus number to use(including this one) */
    uint8_t bus_nr;
    uint16_t numa_node;
} PXBDev;

static PXBDev *convert_to_pxb(PCIDevice *dev)
{
    return pci_bus_is_express(pci_get_bus(dev))
        ? PXB_PCIE_DEV(dev) : PXB_DEV(dev);
}

static GList *pxb_dev_list;

#define TYPE_PXB_HOST "pxb-host"
#define TYPE_PXB_PCIE_HOST "pxb-pcie-host"
#define PXB_PCIE_HOST_DEVICE(obj) \
     OBJECT_CHECK(PXBPCIEHost, (obj), TYPE_PXB_PCIE_HOST)

typedef struct PXBPCIEHost {
    PCIExpressHost parent_obj;

    /* pointers to PXBDev */
    PXBDev *pxbdev;
} PXBPCIEHost;

static int pxb_bus_num(PCIBus *bus)
{
    PXBDev *pxb = convert_to_pxb(bus->parent_dev);

    return pxb->bus_nr;
}

static bool pxb_is_root(PCIBus *bus)
{
    return true; /* by definition */
}

static uint16_t pxb_bus_numa_node(PCIBus *bus)
{
    PXBDev *pxb = convert_to_pxb(bus->parent_dev);

    return pxb->numa_node;
}

static void pxb_bus_class_init(ObjectClass *class, void *data)
{
    PCIBusClass *pbc = PCI_BUS_CLASS(class);

    pbc->bus_num = pxb_bus_num;
    pbc->is_root = pxb_is_root;
    pbc->numa_node = pxb_bus_numa_node;
}

static const TypeInfo pxb_bus_info = {
    .name          = TYPE_PXB_BUS,
    .parent        = TYPE_PCI_BUS,
    .instance_size = sizeof(PXBBus),
    .class_init    = pxb_bus_class_init,
};

static const TypeInfo pxb_pcie_bus_info = {
    .name          = TYPE_PXB_PCIE_BUS,
    .parent        = TYPE_PCIE_BUS,
    .instance_size = sizeof(PXBBus),
    .class_init    = pxb_bus_class_init,
};

static uint64_t pxb_mcfg_hole_size = 0;

static void pxb_pcie_foreach(gpointer data, gpointer user_data)
{
    PXBDev *pxb = (PXBDev *)data;

    if (pxb->domain_nr > 0) {
        /* only reserve what users ask for to reduce memory cost. Plus one
         * as the interval [bus_nr, max_bus] has (max_bus-bus_nr+1) buses */
        pxb_mcfg_hole_size += ((pxb->max_bus - pxb->bus_nr + 1ULL) * MiB);
    }
}

uint64_t pxb_pcie_mcfg_hole(void)
{
    /* foreach is necessary as some pxb still reside in domain 0 */
    g_list_foreach(pxb_dev_list, pxb_pcie_foreach, NULL);
    return pxb_mcfg_hole_size;
}

static const char *pxb_host_root_bus_path(PCIHostState *host_bridge,
                                          PCIBus *rootbus)
{
    PXBBus *bus = pci_bus_is_express(rootbus) ?
                  PXB_PCIE_BUS(rootbus) : PXB_BUS(rootbus);

    snprintf(bus->bus_path, 8, "0000:%02x", pxb_bus_num(rootbus));
    return bus->bus_path;
}

/* Use a dedicated function for PCIe since pxb-host does
 * not have a domain_nr field */
static const char *pxb_pcie_host_root_bus_path(PCIHostState *host_bridge,
                                          PCIBus *rootbus)
{
    if (!pci_bus_is_express(rootbus)) {
        /* pxb-pcie-host cannot reside on a PCI bus */
        return NULL;
    }
    PXBBus *bus = PXB_PCIE_BUS(rootbus);

    /* get the pointer to PXBDev */
    Object *obj = object_property_get_link(OBJECT(host_bridge),
                                           PROP_PXB_PCIE_DEV, NULL);

    snprintf(bus->bus_path, 8, "%04lx:%02x",
             object_property_get_uint(obj, PROP_PXB_PCIE_DOMAIN_NR, NULL),
             pxb_bus_num(rootbus));
    return bus->bus_path;
}

static char *pxb_host_ofw_unit_address(const SysBusDevice *dev)
{
    const PCIHostState *pxb_host;
    const PCIBus *pxb_bus;
    const PXBDev *pxb_dev;
    int position;
    const DeviceState *pxb_dev_base;
    const PCIHostState *main_host;
    const SysBusDevice *main_host_sbd;

    pxb_host = PCI_HOST_BRIDGE(dev);
    pxb_bus = pxb_host->bus;
    pxb_dev = convert_to_pxb(pxb_bus->parent_dev);
    position = g_list_index(pxb_dev_list, pxb_dev);
    assert(position >= 0);

    pxb_dev_base = DEVICE(pxb_dev);
    main_host = PCI_HOST_BRIDGE(pxb_dev_base->parent_bus->parent);
    main_host_sbd = SYS_BUS_DEVICE(main_host);

    if (main_host_sbd->num_mmio > 0) {
        return g_strdup_printf(TARGET_FMT_plx ",%x",
                               main_host_sbd->mmio[0].addr, position + 1);
    }
    if (main_host_sbd->num_pio > 0) {
        return g_strdup_printf("i%04x,%x",
                               main_host_sbd->pio[0], position + 1);
    }
    return NULL;
}

static void pxb_pcie_host_initfn(Object *obj)
{
    PXBPCIEHost *s = PXB_PCIE_HOST_DEVICE(obj);
    PCIHostState *phb = PCI_HOST_BRIDGE(obj);

    memory_region_init_io(&phb->conf_mem, obj, &pci_host_conf_le_ops, phb,
                          "pci-conf-idx", 4);
    memory_region_init_io(&phb->data_mem, obj, &pci_host_data_le_ops, phb,
                          "pci-conf-data", 4);

    object_property_add_link(obj, PROP_PXB_PCIE_DEV, TYPE_PXB_PCIE_DEVICE,
                         (Object **)&s->pxbdev,
                         qdev_prop_allow_set_link_before_realize, 0, NULL);
}

static void pxb_pcie_host_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    // FIX ME! Use specific port number for pxb-pcie host bridge, not scalable!
    /* port layout is | pxb1_cmd | pxb1_data | pxb2_cmd | pxb2_data | ... | */
    sysbus_add_io(sbd, PXB_PCIE_HOST_BRIDGE_CONFIG_ADDR_BASE, &pci->conf_mem);
    sysbus_init_ioports(sbd, PXB_PCIE_HOST_BRIDGE_CONFIG_ADDR_BASE + g_list_length(pxb_dev_list) * 8, 4);

    sysbus_add_io(sbd, PXB_PCIE_HOST_BRIDGE_CONFIG_DATA_BASE, &pci->data_mem);
    sysbus_init_ioports(sbd, PXB_PCIE_HOST_BRIDGE_CONFIG_DATA_BASE + g_list_length(pxb_dev_list) * 8, 4);
}

static Property pxb_pcie_host_props[] = {
    DEFINE_PROP_UINT64(PCIE_HOST_MCFG_BASE, PXBPCIEHost, parent_obj.base_addr,
                        PCIE_BASE_ADDR_UNMAPPED),
    DEFINE_PROP_UINT64(PCIE_HOST_MCFG_SIZE, PXBPCIEHost, parent_obj.size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pxb_host_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(class);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(class);

    dc->fw_name = "pci";
    /* Reason: Internal part of the pxb/pxb-pcie device, not usable by itself */
    dc->user_creatable = false;
    sbc->explicit_ofw_unit_address = pxb_host_ofw_unit_address;
    hc->root_bus_path = pxb_host_root_bus_path;
}

static void pxb_pcie_host_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(class);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(class);

    dc->fw_name = "pcie";
    dc->props = pxb_pcie_host_props;
    dc->realize = pxb_pcie_host_realize;
    /* Reason: Internal part of the pxb/pxb-pcie device, not usable by itself */
    dc->user_creatable = false;
    sbc->explicit_ofw_unit_address = pxb_host_ofw_unit_address;
    hc->root_bus_path = pxb_pcie_host_root_bus_path;
}

static const TypeInfo pxb_host_info = {
    .name          = TYPE_PXB_HOST,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .class_init    = pxb_host_class_init,
};

static const TypeInfo pxb_pcie_host_info = {
    .name          = TYPE_PXB_PCIE_HOST,
    .parent        = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(PXBPCIEHost),
    .instance_init = pxb_pcie_host_initfn,
    .class_init    = pxb_pcie_host_class_init,
};

/*
 * Registers the PXB bus as a child of pci host root bus.
 */
static void pxb_register_bus(PCIDevice *dev, PCIBus *pxb_bus, Error **errp)
{
    PCIBus *bus = pci_get_bus(dev);
    int pxb_bus_num = pci_bus_num(pxb_bus);

    if (bus->parent_dev) {
        error_setg(errp, "PXB devices can be attached only to root bus");
        return;
    }

    QLIST_FOREACH(bus, &bus->child, sibling) {
        if (pci_bus_num(bus) == pxb_bus_num) {
            error_setg(errp, "Bus %d is already in use", pxb_bus_num);
            return;
        }
    }
    QLIST_INSERT_HEAD(&pci_get_bus(dev)->child, pxb_bus, sibling);
}

static int pxb_map_irq_fn(PCIDevice *pci_dev, int pin)
{
    PCIDevice *pxb = pci_get_bus(pci_dev)->parent_dev;

    /*
     * The bios does not index the pxb slot number when
     * it computes the IRQ because it resides on bus 0
     * and not on the current bus.
     * However QEMU routes the irq through bus 0 and adds
     * the pxb slot to the IRQ computation of the PXB
     * device.
     *
     * Synchronize between bios and QEMU by canceling
     * pxb's effect.
     */
    return pin - PCI_SLOT(pxb->devfn);
}

static gint pxb_compare(gconstpointer a, gconstpointer b)
{
    const PXBDev *pxb_a = a, *pxb_b = b;

    /* check domain_nr, then bus_nr */
    return pxb_a->domain_nr < pxb_b->domain_nr ? -1 :
           pxb_a->domain_nr > pxb_b->domain_nr ?  1 :
           pxb_a->bus_nr < pxb_b->bus_nr ? -1 :
           pxb_a->bus_nr > pxb_b->bus_nr ?  1 :
           0;
}

static uint64_t pxb_pcie_mcfg_base;

static void pxb_dev_realize_common(PCIDevice *dev, bool pcie, Error **errp)
{
    PXBDev *pxb = convert_to_pxb(dev);
    DeviceState *ds, *bds = NULL;
    PCIBus *bus;
    const char *dev_name = NULL;
    Error *local_err = NULL;

    if (pxb->numa_node != NUMA_NODE_UNASSIGNED &&
        pxb->numa_node >= nb_numa_nodes) {
        error_setg(errp, "Illegal numa node %d", pxb->numa_node);
        return;
    }

    if (dev->qdev.id && *dev->qdev.id) {
        dev_name = dev->qdev.id;
    }

    if (pcie) {
        g_assert (pxb->max_bus >= pxb->bus_nr);
        ds = qdev_create(NULL, TYPE_PXB_PCIE_HOST);
        /* attach it under /machine, so that we can resolve a valid path in
         * object_property_set_link below */
        object_property_add_child(qdev_get_machine(), "pxb-pcie-host[*]", OBJECT(ds), NULL);

        /* set link and backlink between PXBPCIEHost and PXBDev */
        object_property_set_link(OBJECT(ds), OBJECT(pxb),
                                 PROP_PXB_PCIE_DEV, errp);
        object_property_set_link(OBJECT(pxb), OBJECT(ds),
                                 PROP_PXB_PCIE_HOST, errp);

        /* will be overwritten by firmware, but kept for readability */
        qdev_prop_set_uint64(ds, PCIE_HOST_MCFG_BASE,
            pxb->domain_nr ? pxb_pcie_mcfg_base : MCH_HOST_BRIDGE_PCIEXBAR_DEFAULT);
        /* +1 because [bus_nr, max_bus] has (max_bus-bus_nr+1) buses */
        qdev_prop_set_uint64(ds, PCIE_HOST_MCFG_SIZE,
            pxb->domain_nr ? (pxb->max_bus - pxb->bus_nr + 1ULL) * MiB : 0);
        if (pxb->domain_nr)
            pxb_pcie_mcfg_base += ((pxb->max_bus + 1ULL) * MiB);

        bus = pci_root_bus_new(ds, dev_name, NULL, NULL, 0, TYPE_PXB_PCIE_BUS);
    } else {
        ds = qdev_create(NULL, TYPE_PXB_HOST);
        bus = pci_root_bus_new(ds, "pxb-internal", NULL, NULL, 0, TYPE_PXB_BUS);
        bds = qdev_create(BUS(bus), "pci-bridge");
        bds->id = dev_name;
        qdev_prop_set_uint8(bds, PCI_BRIDGE_DEV_PROP_CHASSIS_NR, pxb->bus_nr);
        qdev_prop_set_bit(bds, PCI_BRIDGE_DEV_PROP_SHPC, false);
    }

    bus->parent_dev = dev;
    bus->address_space_mem = pci_get_bus(dev)->address_space_mem;
    bus->address_space_io = pci_get_bus(dev)->address_space_io;
    bus->map_irq = pxb_map_irq_fn;

    PCI_HOST_BRIDGE(ds)->bus = bus;

    pxb_register_bus(dev, bus, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto err_register_bus;
    }

    qdev_init_nofail(ds);
    if (bds) {
        qdev_init_nofail(bds);
    }

    pci_word_test_and_set_mask(dev->config + PCI_STATUS,
                               PCI_STATUS_66MHZ | PCI_STATUS_FAST_BACK);
    pci_config_set_class(dev->config, PCI_CLASS_BRIDGE_HOST);

    pxb_dev_list = g_list_insert_sorted(pxb_dev_list, pxb, pxb_compare);
    return;

err_register_bus:
    object_unref(OBJECT(bds));
    object_unparent(OBJECT(bus));
    object_unref(OBJECT(ds));
}

static void pxb_dev_realize(PCIDevice *dev, Error **errp)
{
    if (pci_bus_is_express(pci_get_bus(dev))) {
        error_setg(errp, "pxb devices cannot reside on a PCIe bus");
        return;
    }

    pxb_dev_realize_common(dev, false, errp);
}

static void pxb_dev_exitfn(PCIDevice *pci_dev)
{
    PXBDev *pxb = convert_to_pxb(pci_dev);

    pxb_dev_list = g_list_remove(pxb_dev_list, pxb);
}

static uint32_t pxb_pcie_config_read(PCIDevice *d, uint32_t address, int len)
{
    PXBDev *pxb = convert_to_pxb(d);
    uint32_t val;
    Object *host;

   switch (address) {
    case MCH_HOST_BRIDGE_PCIEXBAR:
        host = object_property_get_link(OBJECT(pxb), PROP_PXB_PCIE_HOST, NULL);
        assert(host);
        val = object_property_get_uint(host, PCIE_HOST_MCFG_BASE, NULL) & 0xFFFFFFFF;
        break;
    case MCH_HOST_BRIDGE_PCIEXBAR + 4:
        host = object_property_get_link(OBJECT(pxb), PROP_PXB_PCIE_HOST, NULL);
        assert(host);
        val = (object_property_get_uint(host, PCIE_HOST_MCFG_BASE, NULL) >> 32) & 0xFFFFFFFF;
        break;
    case MCH_HOST_BRIDGE_PCIEXBAR + 8:  // Fix me!
        host = object_property_get_link(OBJECT(pxb), PROP_PXB_PCIE_HOST, NULL);
        assert(host);
        val = object_property_get_uint(host, PCIE_HOST_MCFG_SIZE, NULL) & 0xFFFFFFFF;
        break;
    default:
        val = pci_default_read_config(d, address, len);
        break;
    }

    return val;
}

static Property pxb_dev_properties[] = {
    /* Note: 0 is not a legal PXB bus number. */
    DEFINE_PROP_UINT8(PROP_PXB_BUS_NR, PXBDev, bus_nr, 0),
    DEFINE_PROP_UINT16(PROP_PXB_NUMA_NODE, PXBDev, numa_node, NUMA_NODE_UNASSIGNED),
    DEFINE_PROP_END_OF_LIST(),
};

static Property pxb_pcie_dev_properties[] = {
    DEFINE_PROP_UINT8(PROP_PXB_BUS_NR, PXBDev, bus_nr, 0),
    DEFINE_PROP_UINT16(PROP_PXB_NUMA_NODE, PXBDev, numa_node, NUMA_NODE_UNASSIGNED),
    DEFINE_PROP_UINT32(PROP_PXB_PCIE_DOMAIN_NR, PXBDev, domain_nr, 0),
    /* set a small default value, bus interval is [bus_nr, max_bus] */
    DEFINE_PROP_UINT8(PROP_PXB_PCIE_MAX_BUS, PXBDev, max_bus, 16),

    DEFINE_PROP_END_OF_LIST(),
};

static void pxb_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pxb_dev_realize;
    k->exit = pxb_dev_exitfn;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PXB;
    k->class_id = PCI_CLASS_BRIDGE_HOST;

    dc->desc = "PCI Expander Bridge";
    dc->props = pxb_dev_properties;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static void pxb_pcie_dev_initfn(Object *obj)
{
    PXBDev *pxb = PXB_PCIE_DEV(obj);

    /* Add backlink to pxb-pcie-host */
    object_property_add_link(obj, PROP_PXB_PCIE_HOST, TYPE_PXB_PCIE_HOST,
                         (Object **)&pxb->pxbhost,
                         qdev_prop_allow_set_link_before_realize, 0, NULL);
}

static const TypeInfo pxb_dev_info = {
    .name          = TYPE_PXB_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PXBDev),
    .class_init    = pxb_dev_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pxb_pcie_dev_realize(PCIDevice *dev, Error **errp)
{
    if (!pci_bus_is_express(pci_get_bus(dev))) {
        error_setg(errp, "pxb-pcie devices cannot reside on a PCI bus");
        return;
    }

    if (0 == pxb_pcie_mcfg_base)
        pxb_pcie_mcfg_base = pc_pci_mcfg_start();

    pxb_dev_realize_common(dev, true, errp);
}

static void pxb_pcie_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pxb_pcie_dev_realize;
    k->exit = pxb_dev_exitfn;
    k->config_read = pxb_pcie_config_read;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PXB_PCIE;
    k->class_id = PCI_CLASS_BRIDGE_HOST;

    dc->desc = "PCI Express Expander Bridge";
    dc->props = pxb_pcie_dev_properties;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo pxb_pcie_dev_info = {
    .name          = TYPE_PXB_PCIE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PXBDev),
    .instance_init = pxb_pcie_dev_initfn,
    .class_init    = pxb_pcie_dev_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pxb_register_types(void)
{
    type_register_static(&pxb_bus_info);
    type_register_static(&pxb_pcie_bus_info);
    type_register_static(&pxb_host_info);
    type_register_static(&pxb_pcie_host_info);
    type_register_static(&pxb_dev_info);
    type_register_static(&pxb_pcie_dev_info);
}

type_init(pxb_register_types)
