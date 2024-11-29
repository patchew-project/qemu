/*
 * Guest driven VM boot component update device
 * For details and specification, please look at docs/specs/vmfwupdate.rst.
 *
 * Copyright (C) 2024 Red Hat, Inc.
 *
 * Authors: Ani Sinha <anisinha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "sysemu/reset.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/i386/pc.h"
#include "hw/qdev-properties.h"
#include "hw/misc/vmfwupdate.h"
#include "qemu/error-report.h"

static void fw_update_reset(void *dev)
{
    /* a NOOP at present */
    return;
}


static uint64_t get_max_fw_size(void)
{
    Object *m_obj = qdev_get_machine();
    PCMachineState *pcms = PC_MACHINE(m_obj);

    if (pcms) {
        return pcms->max_fw_size;
    } else {
        return 0;
    }
}

static void fw_blob_write(void *dev, off_t offset, size_t len)
{
    VMFwUpdateState *s = VMFWUPDATE(dev);

    /*
     * in order to change the bios size, appropriate capability
       must be enabled
    */
    if (s->fw_blob.bios_size &&
        !(s->capability & VMFWUPDATE_CAP_BIOS_RESIZE)) {
        warn_report("vmfwupdate: VMFWUPDATE_CAP_BIOS_RESIZE not enabled");
        return;
    }

    s->plat_bios_size = s->fw_blob.bios_size;

    return;
}

static void vmfwupdate_realize(DeviceState *dev, Error **errp)
{
    VMFwUpdateState *s = VMFWUPDATE(dev);
    FWCfgState *fw_cfg = fw_cfg_find();

    /* multiple devices are not supported */
    if (!vmfwupdate_find()) {
        error_setg(errp, "at most one %s device is permitted",
                   TYPE_VMFWUPDATE);
        return;
    }

    /* fw_cfg with DMA support is necessary to support this device */
    if (!fw_cfg || !fw_cfg_dma_enabled(fw_cfg)) {
        error_setg(errp, "%s device requires fw_cfg",
                   TYPE_VMFWUPDATE);
        return;
    }

    memset(&s->fw_blob, 0, sizeof(s->fw_blob));
    memset(&s->opaque_blobs, 0, sizeof(s->opaque_blobs));

    fw_cfg_add_file_callback(fw_cfg, FILE_VMFWUPDATE_OBLOB,
                             NULL, NULL, s,
                             &s->opaque_blobs,
                             sizeof(s->opaque_blobs),
                             false);

    fw_cfg_add_file_callback(fw_cfg, FILE_VMFWUPDATE_FWBLOB,
                             NULL, fw_blob_write, s,
                             &s->fw_blob,
                             sizeof(s->fw_blob),
                             false);

    /*
     * Add global capability fw_cfg file. This will be used by the guest to
     * check capability of the hypervisor.
     */
    s->capability = cpu_to_le16(CAP_VMFWUPD_MASK | VMFWUPDATE_CAP_EDKROM);
    fw_cfg_add_file(fw_cfg, FILE_VMFWUPDATE_CAP,
                    &s->capability, sizeof(s->capability));

    s->plat_bios_size = get_max_fw_size();
    /* size of bios region for the platform - read only by the guest */
    fw_cfg_add_file(fw_cfg, FILE_VMFWUPDATE_BIOS_SIZE,
                    &s->plat_bios_size, sizeof(s->plat_bios_size));
    /*
     * add fw cfg control file to disable the hypervisor interface.
     */
    fw_cfg_add_file_callback(fw_cfg, FILE_VMFWUPDATE_CONTROL,
                             NULL, NULL, s,
                             &s->disable,
                             sizeof(s->disable),
                             false);
    /*
     * This device requires to register a global reset because it is
     * not plugged to a bus (which, as its QOM parent, would reset it).
     */
    qemu_register_reset(fw_update_reset, dev);
}

static Property vmfwupdate_properties[] = {
    DEFINE_PROP_UINT8("disable", VMFwUpdateState, disable, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void vmfwupdate_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /* we are not interested in migration - so no need to populate dc->vmsd */
    dc->desc = "VM firmware blob update device";
    dc->realize = vmfwupdate_realize;
    dc->hotpluggable = false;
    device_class_set_props(dc, vmfwupdate_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo vmfwupdate_device_info = {
    .name          = TYPE_VMFWUPDATE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(VMFwUpdateState),
    .class_init    = vmfwupdate_device_class_init,
};

static void vmfwupdate_register_types(void)
{
    type_register_static(&vmfwupdate_device_info);
}

type_init(vmfwupdate_register_types);
