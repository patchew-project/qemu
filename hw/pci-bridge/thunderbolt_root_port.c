/*
 * Thunderbolt PCI Express Root Port
 *
 * This root port creates a Thunderbolt-flavoured PCIe bus. Devices on that
 * bus remain regular QEMU PCI devices; the Thunderbolt layer controls their
 * guest-visible connection state.
 *
 * Copyright (C) 2026
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/pci.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "hw/core/hotplug.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie_port.h"
#include "migration/vmstate.h"
#include "system/system.h"
#include "qom/object.h"

#define TYPE_THUNDERBOLT_ROOT_PORT "thunderbolt-root-port"
#define TYPE_THUNDERBOLT_BUS "thunderbolt-bus"

OBJECT_DECLARE_SIMPLE_TYPE(ThunderboltRootPort, THUNDERBOLT_ROOT_PORT)

#define THUNDERBOLT_ROOT_PORT_AER_OFFSET 0x100
#define THUNDERBOLT_ROOT_PORT_ACS_OFFSET \
    (THUNDERBOLT_ROOT_PORT_AER_OFFSET + PCI_ERR_SIZEOF)
#define THUNDERBOLT_ROOT_PORT_MSIX_NR_VECTOR 1
#define THUNDERBOLT_ROOT_DEFAULT_IO_RANGE 4096

struct ThunderboltRootPort {
    PCIESlot parent_obj;

    bool migrate_msix;
    PCIResReserve res_reserve;
    bool connected;
    DeviceListener listener;
    Notifier machine_done;
};

static void thunderbolt_root_port_set_presence(PCIDevice *dev, bool present,
                                               bool notify);
static void thunderbolt_root_port_sync_connected(ThunderboltRootPort *trp,
                                                 bool notify);

static Aml *build_device_properties_dsd(Aml *properties)
{
    Aml *dsd;

    dsd = aml_package(2);
    aml_append(dsd, aml_touuid("DAFFD814-6EBA-4D8C-8A91-BC9BBF4AA301"));
    aml_append(dsd, properties);

    return aml_name_decl("_DSD", dsd);
}

static void append_bool_property(Aml *properties, const char *name)
{
    Aml *property = aml_package(2);

    aml_append(property, aml_string("%s", name));
    aml_append(property, aml_int(1));
    aml_append(properties, property);
}

static void build_thunderbolt_root_port_dsd(Aml *scope)
{
    Aml *properties;

    properties = aml_package(2);
    append_bool_property(properties, "PCI-Thunderbolt");
    append_bool_property(properties, "pci-supports-link-change");

    aml_append(scope, build_device_properties_dsd(properties));
}

static void build_thunderbolt_root_port_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    build_thunderbolt_root_port_dsd(scope);
    build_pci_bridge_aml(adev, scope);
}

static bool thunderbolt_root_port_get_connected(Object *obj, Error **errp)
{
    return THUNDERBOLT_ROOT_PORT(obj)->connected;
}

static void thunderbolt_root_port_set_connected(Object *obj, bool value,
                                                Error **errp)
{
    ThunderboltRootPort *trp = THUNDERBOLT_ROOT_PORT(obj);

    if (trp->connected == value) {
        return;
    }

    trp->connected = value;

    if (!qdev_is_realized(DEVICE(obj))) {
        return;
    }

    thunderbolt_root_port_sync_connected(trp, true);
}

static void thunderbolt_root_port_sync_connected(ThunderboltRootPort *trp,
                                                 bool notify)
{
    PCIDevice *rp = PCI_DEVICE(trp);
    PCIBus *bus = pci_bridge_get_sec_bus(PCI_BRIDGE(rp));
    bool present = false;
    int devfn;

    for (devfn = 0; devfn < PCI_DEVFN_MAX; devfn++) {
        PCIDevice *child = bus->devices[devfn];

        if (!child) {
            continue;
        }

        present = true;
        if (trp->connected) {
            pci_set_enabled(child, true);
        }
    }

    if (trp->connected && present) {
        thunderbolt_root_port_set_presence(rp, true, notify);
    } else {
        thunderbolt_root_port_set_presence(rp, false, notify);
        for (devfn = 0; devfn < PCI_DEVFN_MAX; devfn++) {
            PCIDevice *child = bus->devices[devfn];

            if (child) {
                pci_set_enabled(child, false);
            }
        }
    }
}

static void thunderbolt_root_port_machine_done(Notifier *notifier, void *data)
{
    ThunderboltRootPort *trp = container_of(notifier, ThunderboltRootPort,
                                            machine_done);

    thunderbolt_root_port_sync_connected(trp, false);
}

static void thunderbolt_root_port_device_realize(DeviceListener *listener,
                                                 DeviceState *dev)
{
    ThunderboltRootPort *trp = container_of(listener, ThunderboltRootPort,
                                           listener);
    PCIBus *bus;

    if (!dev->parent_bus ||
        !object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE) ||
        !object_dynamic_cast(OBJECT(dev->parent_bus), TYPE_THUNDERBOLT_BUS)) {
        return;
    }

    bus = PCI_BUS(dev->parent_bus);
    if (bus->parent_dev != PCI_DEVICE(trp)) {
        return;
    }

    thunderbolt_root_port_sync_connected(trp, false);
}

static void thunderbolt_root_port_instance_init(Object *obj)
{
    ThunderboltRootPort *trp = THUNDERBOLT_ROOT_PORT(obj);

    trp->connected = true;
    object_property_add_bool(obj, "connected",
                             thunderbolt_root_port_get_connected,
                             thunderbolt_root_port_set_connected);
}

static void thunderbolt_root_port_set_presence(PCIDevice *dev, bool present,
                                               bool notify)
{
    uint8_t *exp_cap = dev->config + dev->exp.exp_cap;
    uint32_t lnkcap = pci_get_long(exp_cap + PCI_EXP_LNKCAP);

    if (present) {
        pcie_cap_slot_enable_power(dev);
        pci_word_test_and_set_mask(exp_cap + PCI_EXP_SLTSTA,
                                   PCI_EXP_SLTSTA_PDS);
        if (dev->cap_present & QEMU_PCIE_LNKSTA_DLLLA ||
            (lnkcap & PCI_EXP_LNKCAP_DLLLARC)) {
            pci_word_test_and_set_mask(exp_cap + PCI_EXP_LNKSTA,
                                       PCI_EXP_LNKSTA_DLLLA);
        }
    } else {
        pci_word_test_and_clear_mask(exp_cap + PCI_EXP_SLTSTA,
                                     PCI_EXP_SLTSTA_PDS);
        if (dev->cap_present & QEMU_PCIE_LNKSTA_DLLLA ||
            (lnkcap & PCI_EXP_LNKCAP_DLLLARC)) {
            pci_word_test_and_clear_mask(exp_cap + PCI_EXP_LNKSTA,
                                         PCI_EXP_LNKSTA_DLLLA);
        }
    }

    if (notify) {
        pci_word_test_and_set_mask(exp_cap + PCI_EXP_SLTSTA,
                                   PCI_EXP_SLTSTA_PDC);
        pcie_cap_slot_push_attention_button(dev);
    }
}

static void thunderbolt_root_port_pre_plug(HotplugHandler *hotplug_dev,
                                           DeviceState *dev, Error **errp)
{
    pcie_cap_slot_pre_plug_cb(hotplug_dev, dev, errp);
}

static void thunderbolt_root_port_plug(HotplugHandler *hotplug_dev,
                                       DeviceState *dev, Error **errp)
{
    ThunderboltRootPort *trp = THUNDERBOLT_ROOT_PORT(hotplug_dev);

    if (trp->connected) {
        pcie_cap_slot_plug_cb(hotplug_dev, dev, errp);
        return;
    }

    pci_set_enabled(PCI_DEVICE(dev), false);
    thunderbolt_root_port_set_presence(PCI_DEVICE(hotplug_dev), false, false);
}

static void thunderbolt_root_port_unplug_request(HotplugHandler *hotplug_dev,
                                                 DeviceState *dev,
                                                 Error **errp)
{
    pcie_cap_slot_unplug_request_cb(hotplug_dev, dev, errp);
}

static void thunderbolt_root_port_unplug(HotplugHandler *hotplug_dev,
                                         DeviceState *dev, Error **errp)
{
    pcie_cap_slot_unplug_cb(hotplug_dev, dev, errp);
}

static uint8_t thunderbolt_root_port_aer_vector(const PCIDevice *dev)
{
    return 0;
}

static void thunderbolt_root_port_aer_vector_update(PCIDevice *dev)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(dev);

    if (rpc->aer_vector) {
        pcie_aer_root_set_vector(dev, rpc->aer_vector(dev));
    }
}

static int thunderbolt_root_port_interrupts_init(PCIDevice *dev, Error **errp)
{
    int rc;

    rc = msix_init_exclusive_bar(dev, THUNDERBOLT_ROOT_PORT_MSIX_NR_VECTOR,
                                 0, errp);
    if (rc < 0) {
        assert(rc == -ENOTSUP);
    } else {
        msix_vector_use(dev, 0);
    }

    return rc;
}

static void thunderbolt_root_port_interrupts_uninit(PCIDevice *dev)
{
    msix_uninit_exclusive_bar(dev);
}

static void thunderbolt_root_port_write_config(PCIDevice *dev,
                                               uint32_t address,
                                               uint32_t val, int len)
{
    uint32_t root_cmd =
        pci_get_long(dev->config + dev->exp.aer_cap + PCI_ERR_ROOT_COMMAND);
    uint16_t slt_ctl;
    uint16_t slt_sta;

    pcie_cap_slot_get(dev, &slt_ctl, &slt_sta);

    pci_bridge_write_config(dev, address, val, len);
    thunderbolt_root_port_aer_vector_update(dev);
    pcie_cap_slot_write_config(dev, slt_ctl, slt_sta, address, val, len);
    pcie_aer_write_config(dev, address, val, len);
    pcie_aer_root_write_config(dev, address, val, len, root_cmd);
}

static void thunderbolt_root_port_reset_hold(Object *obj, ResetType type)
{
    ThunderboltRootPort *trp = THUNDERBOLT_ROOT_PORT(obj);
    PCIDevice *dev = PCI_DEVICE(obj);
    DeviceState *qdev = DEVICE(obj);

    thunderbolt_root_port_aer_vector_update(dev);
    pcie_cap_root_reset(dev);
    pcie_cap_deverr_reset(dev);
    pcie_cap_slot_reset(dev);
    pcie_cap_arifwd_reset(dev);
    pcie_acs_reset(dev);
    pcie_aer_root_reset(dev);
    pci_bridge_reset(qdev);
    pci_bridge_disable_base_limit(dev);
    thunderbolt_root_port_sync_connected(trp, false);
}

static void thunderbolt_root_port_realize_pci(PCIDevice *dev, Error **errp)
{
    PCIEPort *p = PCIE_PORT(dev);
    PCIESlot *s = PCIE_SLOT(dev);
    PCIDeviceClass *dc = PCI_DEVICE_GET_CLASS(dev);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(dev);
    int rc;

    pci_config_set_interrupt_pin(dev->config, 1);
    pci_bridge_initfn(dev, TYPE_THUNDERBOLT_BUS);
    pcie_port_init_reg(dev);

    rc = pci_bridge_ssvid_init(dev, rpc->ssvid_offset, dc->vendor_id,
                               rpc->ssid, errp);
    if (rc < 0) {
        error_append_hint(errp, "Can't init SSV ID, error %d\n", rc);
        goto err_bridge;
    }

    if (rpc->interrupts_init) {
        rc = rpc->interrupts_init(dev, errp);
        if (rc < 0) {
            goto err_bridge;
        }
    }

    rc = pcie_cap_init(dev, rpc->exp_offset, PCI_EXP_TYPE_ROOT_PORT,
                       p->port, errp);
    if (rc < 0) {
        error_append_hint(errp, "Can't add Root Port capability, "
                          "error %d\n", rc);
        goto err_int;
    }

    pcie_cap_arifwd_init(dev);
    pcie_cap_deverr_init(dev);
    pcie_cap_slot_init(dev, s);
    qbus_set_hotplug_handler(BUS(pci_bridge_get_sec_bus(PCI_BRIDGE(dev))),
                             OBJECT(dev));
    pcie_cap_root_init(dev);

    pcie_chassis_create(s->chassis);
    rc = pcie_chassis_add_slot(s);
    if (rc < 0) {
        error_setg(errp, "Can't add chassis slot, error %d", rc);
        goto err_pcie_cap;
    }

    rc = pcie_aer_init(dev, PCI_ERR_VER, rpc->aer_offset,
                       PCI_ERR_SIZEOF, errp);
    if (rc < 0) {
        goto err;
    }
    pcie_aer_root_init(dev);
    thunderbolt_root_port_aer_vector_update(dev);

    if (rpc->acs_offset) {
        pcie_acs_init(dev, rpc->acs_offset);
    }

    THUNDERBOLT_ROOT_PORT(dev)->listener.realize =
        thunderbolt_root_port_device_realize;
    device_listener_register(&THUNDERBOLT_ROOT_PORT(dev)->listener);

    THUNDERBOLT_ROOT_PORT(dev)->machine_done.notify =
        thunderbolt_root_port_machine_done;
    qemu_add_machine_init_done_notifier(
        &THUNDERBOLT_ROOT_PORT(dev)->machine_done);

    return;

err:
    pcie_chassis_del_slot(s);
err_pcie_cap:
    pcie_cap_exit(dev);
err_int:
    if (rpc->interrupts_uninit) {
        rpc->interrupts_uninit(dev);
    }
err_bridge:
    pci_bridge_exitfn(dev);
}

static void thunderbolt_root_port_exit(PCIDevice *dev)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(dev);
    PCIESlot *s = PCIE_SLOT(dev);

    pcie_aer_exit(dev);
    pcie_chassis_del_slot(s);
    pcie_cap_exit(dev);
    if (rpc->interrupts_uninit) {
        rpc->interrupts_uninit(dev);
    }
    device_listener_unregister(&THUNDERBOLT_ROOT_PORT(dev)->listener);
    qemu_remove_machine_init_done_notifier(
        &THUNDERBOLT_ROOT_PORT(dev)->machine_done);
    pci_bridge_exitfn(dev);
}

static void thunderbolt_root_port_realize(DeviceState *dev, Error **errp)
{
    ThunderboltRootPort *trp = THUNDERBOLT_ROOT_PORT(dev);
    PCIESlot *slot = PCIE_SLOT(dev);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(dev);
    Error *local_err = NULL;
    int rc;

    rpc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (slot->hide_native_hotplug_cap &&
        trp->res_reserve.io == (uint64_t)-1 && slot->hotplug) {
        trp->res_reserve.io = THUNDERBOLT_ROOT_DEFAULT_IO_RANGE;
    }

    rc = pci_bridge_qemu_reserve_cap_init(PCI_DEVICE(dev), 0,
                                          trp->res_reserve, errp);
    if (rc < 0) {
        rpc->parent_class.exit(PCI_DEVICE(dev));
        return;
    }

    if (!trp->res_reserve.io) {
        PCIDevice *pdev = PCI_DEVICE(dev);

        pci_word_test_and_clear_mask(pdev->wmask + PCI_COMMAND,
                                     PCI_COMMAND_IO);
        pdev->wmask[PCI_IO_BASE] = 0;
        pdev->wmask[PCI_IO_LIMIT] = 0;
    }
}

static bool thunderbolt_root_port_test_migrate_msix(void *opaque,
                                                    int version_id)
{
    ThunderboltRootPort *trp = opaque;

    return trp->migrate_msix;
}

static const VMStateDescription vmstate_thunderbolt_root_port = {
    .name = "thunderbolt-root-port",
    .priority = MIG_PRI_PCI_BUS,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pcie_cap_slot_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent_obj.parent_obj.parent_obj,
                           ThunderboltRootPort),
        VMSTATE_STRUCT(parent_obj.parent_obj.parent_obj.parent_obj.exp.aer_log,
                       ThunderboltRootPort, 0, vmstate_pcie_aer_log,
                       PCIEAERLog),
        VMSTATE_MSIX_TEST(parent_obj.parent_obj.parent_obj.parent_obj,
                          ThunderboltRootPort,
                          thunderbolt_root_port_test_migrate_msix),
        VMSTATE_BOOL(connected, ThunderboltRootPort),
        VMSTATE_END_OF_LIST()
    }
};

static const Property thunderbolt_root_port_props[] = {
    DEFINE_PROP_BOOL("x-migrate-msix", ThunderboltRootPort,
                     migrate_msix, true),
    DEFINE_PROP_UINT32("bus-reserve", ThunderboltRootPort,
                       res_reserve.bus, -1),
    DEFINE_PROP_SIZE("io-reserve", ThunderboltRootPort,
                     res_reserve.io, -1),
    DEFINE_PROP_SIZE("mem-reserve", ThunderboltRootPort,
                     res_reserve.mem_non_pref, -1),
    DEFINE_PROP_SIZE("pref32-reserve", ThunderboltRootPort,
                     res_reserve.mem_pref_32, -1),
    DEFINE_PROP_SIZE("pref64-reserve", ThunderboltRootPort,
                     res_reserve.mem_pref_64, -1),
    DEFINE_PROP_PCIE_LINK_SPEED("x-speed", PCIESlot,
                                speed, PCIE_LINK_SPEED_16),
    DEFINE_PROP_PCIE_LINK_WIDTH("x-width", PCIESlot,
                                width, PCIE_LINK_WIDTH_32),
};

static void thunderbolt_root_port_class_init(ObjectClass *klass,
                                             const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(klass);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PCIE_RP;
    k->config_write = thunderbolt_root_port_write_config;
    k->realize = thunderbolt_root_port_realize_pci;
    k->exit = thunderbolt_root_port_exit;
    dc->desc = "Thunderbolt PCI Express Root Port";
    dc->vmsd = &vmstate_thunderbolt_root_port;
    device_class_set_props(dc, thunderbolt_root_port_props);
    device_class_set_parent_realize(dc, thunderbolt_root_port_realize,
                                    &rpc->parent_realize);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    rc->phases.hold = thunderbolt_root_port_reset_hold;

    hc->pre_plug = thunderbolt_root_port_pre_plug;
    hc->plug = thunderbolt_root_port_plug;
    hc->unplug = thunderbolt_root_port_unplug;
    hc->unplug_request = thunderbolt_root_port_unplug_request;

    rpc->aer_vector = thunderbolt_root_port_aer_vector;
    rpc->interrupts_init = thunderbolt_root_port_interrupts_init;
    rpc->interrupts_uninit = thunderbolt_root_port_interrupts_uninit;
    rpc->aer_offset = THUNDERBOLT_ROOT_PORT_AER_OFFSET;
    rpc->acs_offset = THUNDERBOLT_ROOT_PORT_ACS_OFFSET;
    adevc->build_dev_aml = build_thunderbolt_root_port_aml;
}

static const TypeInfo thunderbolt_root_port_info = {
    .name          = TYPE_THUNDERBOLT_ROOT_PORT,
    .parent        = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(ThunderboltRootPort),
    .instance_init = thunderbolt_root_port_instance_init,
    .class_init    = thunderbolt_root_port_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_ACPI_DEV_AML_IF },
        { },
    },
};

static const TypeInfo thunderbolt_bus_info = {
    .name   = TYPE_THUNDERBOLT_BUS,
    .parent = TYPE_PCIE_BUS,
};

static void thunderbolt_root_port_register_types(void)
{
    type_register_static(&thunderbolt_bus_info);
    type_register_static(&thunderbolt_root_port_info);
}

type_init(thunderbolt_root_port_register_types)
