/*
 *  Virtual Machine Generation ID Device
 *
 *  Copyright (C) 2016 Skyport Systems.
 *
 *  Authors: Ben Warren <ben@skyportsystems.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qmp-commands.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/vmgenid.h"
#include "hw/nvram/fw_cfg.h"
#include "sysemu/sysemu.h"

Object *find_vmgenid_dev(Error **errp)
{
    Object *obj = object_resolve_path_type("", VMGENID_DEVICE, NULL);
    if (!obj) {
        error_setg(errp, VMGENID_DEVICE " is not found");
    }
    return obj;
}

void vmgenid_build_acpi(GArray *table_offsets, GArray *table_data,
        BIOSLinker *linker)
{
    Object *obj = find_vmgenid_dev(NULL);
    if (!obj) {
        return;
    }
    VmGenIdState *s = VMGENID(obj);

    acpi_add_table(table_offsets, table_data);

    GArray *guid = g_array_new(false, true, sizeof(s->guid.data));
    g_array_append_val(guid, s->guid.data);

    Aml *ssdt, *dev, *scope, *pkg, *method;

    /* Put this in a separate SSDT table */
    ssdt = init_aml_allocator();

    /* Reserve space for header */
    acpi_data_push(ssdt->buf, sizeof(AcpiTableHeader));

    /* Storage for the GUID address */
    uint32_t vgia_offset = table_data->len +
        build_append_named_qword(ssdt->buf, "VGIA");
    dev = aml_device("VGEN");
    scope = aml_scope("\\_SB");
    aml_append(dev, aml_name_decl("_HID", aml_string("QEMUVGID")));
    aml_append(dev, aml_name_decl("_CID", aml_string("VM_Gen_Counter")));
    aml_append(dev, aml_name_decl("_DDN", aml_string("VM_Gen_Counter")));

    /* Simple status method to check that address is linked and non-zero */
    method = aml_method("_STA", 0, AML_NOTSERIALIZED);
    Aml *if_ctx = aml_if(aml_equal(aml_name("VGIA"), aml_int(0)));
    aml_append(if_ctx, aml_return(aml_int(0)));
    aml_append(method, if_ctx);
    Aml *else_ctx = aml_else();
    aml_append(else_ctx, aml_return(aml_int(0xf)));
    aml_append(method, else_ctx);
    aml_append(dev, method);

    /* the ADDR method returns two 32-bit words representing the lower and
     * upper halves * of the physical address of the fw_cfg blob
     * (holding the GUID) */
    method = aml_method("ADDR", 0, AML_NOTSERIALIZED);

    pkg = aml_package(2);
    aml_append(pkg, aml_int(0));
    aml_append(pkg, aml_int(0));

    aml_append(method, aml_name_decl("LPKG", pkg));
    aml_append(method, aml_store(aml_and(aml_name("VGIA"),
        aml_int(0xffffffff), NULL), aml_index(aml_name("LPKG"), aml_int(0))));
    aml_append(method, aml_store(aml_shiftright(aml_name("VGIA"),
        aml_int(32), NULL), aml_index(aml_name("LPKG"), aml_int(1))));
    aml_append(method, aml_return(aml_name("LPKG")));

    aml_append(dev, method);
    aml_append(scope, dev);
    aml_append(ssdt, scope);

    /* attach an ACPI notify */
    scope = aml_scope("_GPE");
    method = aml_method("_E00", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_notify(aml_name("\\_SB.VGEN"), aml_int(0x80)));
    aml_append(scope, method);
    aml_append(ssdt, scope);

    /* copy AML table into ACPI tables blob and patch in fw_cfg blob */
    g_array_append_vals(table_data, ssdt->buf->data, ssdt->buf->len);
    bios_linker_loader_alloc(linker, VMGENID_FW_CFG_FILE, guid, 0,
                             false /* high memory */);
    bios_linker_loader_add_pointer(linker,
        ACPI_BUILD_TABLE_FILE, vgia_offset, sizeof(uint64_t),
        VMGENID_FW_CFG_FILE, 0);

    build_header(linker, table_data,
        (void *)(table_data->data + table_data->len - ssdt->buf->len),
        "SSDT", ssdt->buf->len, 1, NULL, "VMGENID");
    free_aml_allocator();
}

void vmgenid_add_fw_cfg(FWCfgState *s)
{
    Object *obj = find_vmgenid_dev(NULL);
    if (!obj) {
        return;
    }
    VmGenIdState *vms = VMGENID(obj);
    fw_cfg_add_file(s, VMGENID_FW_CFG_FILE, vms->guid.data,
        sizeof(vms->guid.data));
}

static void vmgenid_notify_guest(VmGenIdState *s)
{
    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
    if (obj) {
        /* Send _GPE.E00 event */
        acpi_send_event(DEVICE(obj), 1 << 0);
    }
}

static void vmgenid_set_guid(Object *obj, const char *value, Error **errp)
{
    VmGenIdState *s = VMGENID(obj);

    if (qemu_uuid_parse(value, &s->guid) < 0) {
        error_setg(errp, "'%s." VMGENID_GUID
                   "': Failed to parse GUID string: %s",
                   object_get_typename(OBJECT(s)),
                   value);
        return;
    }
    vmgenid_notify_guest(s);
}

static void vmgenid_state_change(void *priv, int running, RunState state)
{
    VmGenIdState *s;

    if (state != RUN_STATE_RUNNING) {
        return;
    }
    s = VMGENID((Object *)priv);
    vmgenid_notify_guest(s);
}

static void vmgenid_initfn(Object *obj)
{
    object_property_add_str(obj, VMGENID_GUID, NULL, vmgenid_set_guid, NULL);
    qemu_add_vm_change_state_handler(vmgenid_state_change, obj);
}

static const TypeInfo vmgenid_device_info = {
    .name          = VMGENID_DEVICE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VmGenIdState),
    .instance_init = vmgenid_initfn,
};

static void vmgenid_register_types(void)
{
    type_register_static(&vmgenid_device_info);
}

type_init(vmgenid_register_types)

GuidInfo *qmp_query_vm_generation_id(Error **errp)
{
    GuidInfo *info;
    VmGenIdState *vdev;
    Object *obj = find_vmgenid_dev(errp);

    if (!obj) {
        return NULL;
    }
    vdev = VMGENID(obj);
    info = g_malloc0(sizeof(*info));
    info->guid = g_strdup_printf(UUID_FMT, vdev->guid.data[0],
            vdev->guid.data[1], vdev->guid.data[2], vdev->guid.data[3],
            vdev->guid.data[4], vdev->guid.data[5], vdev->guid.data[6],
            vdev->guid.data[7], vdev->guid.data[8], vdev->guid.data[9],
            vdev->guid.data[10], vdev->guid.data[11], vdev->guid.data[12],
            vdev->guid.data[13], vdev->guid.data[14], vdev->guid.data[15]);
    return info;
}

void qmp_set_vm_generation_id(const char *guid, Error **errp)
{
    Object *obj = find_vmgenid_dev(errp);

    if (!obj) {
        return;
    }

    object_property_set_str(obj, guid, VMGENID_GUID, errp);
    return;
}
