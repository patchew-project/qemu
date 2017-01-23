/*
 *  Virtual Machine Generation ID Device
 *
 *  Copyright (C) 2017 Skyport Systems.
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
    Object *obj;
    VmGenIdState *s;
    GArray *guid;
    Aml *ssdt, *dev, *scope, *method, *addr, *if_ctx;
    uint32_t vgia_offset;

    obj = find_vmgenid_dev(NULL);
    if (!obj) {
        return;
    }
    s = VMGENID(obj);

    acpi_add_table(table_offsets, table_data);

    guid = g_array_new(false, true, sizeof(s->guid.data));
    g_array_append_val(guid, s->guid.data);

    /* Put this in a separate SSDT table */
    ssdt = init_aml_allocator();

    /* Reserve space for header */
    acpi_data_push(ssdt->buf, sizeof(AcpiTableHeader));

    /* Storage for the GUID address */
    vgia_offset = table_data->len +
        build_append_named_qword(ssdt->buf, "VGIA");
    scope = aml_scope("\\_SB");
    dev = aml_device("VGEN");
    aml_append(dev, aml_name_decl("_HID", aml_string("QEMUVGID")));
    aml_append(dev, aml_name_decl("_CID", aml_string("VM_Gen_Counter")));
    aml_append(dev, aml_name_decl("_DDN", aml_string("VM_Gen_Counter")));

    /* Simple status method to check that address is linked and non-zero */
    method = aml_method("_STA", 0, AML_NOTSERIALIZED);
    addr = aml_local(0);
    aml_append(method, aml_store(aml_int(0xf), addr));
    if_ctx = aml_if(aml_equal(aml_name("VGIA"), aml_int(0)));
    aml_append(if_ctx, aml_store(aml_int(0), addr));
    aml_append(method, if_ctx);
    aml_append(method, aml_return(addr));
    aml_append(dev, method);

    /* the ADDR method returns two 32-bit words representing the lower and
     * upper halves * of the physical address of the fw_cfg blob
     * (holding the GUID) */
    method = aml_method("ADDR", 0, AML_NOTSERIALIZED);

    addr = aml_local(0);
    aml_append(method, aml_store(aml_package(2), addr));

    aml_append(method, aml_store(aml_and(aml_name("VGIA"),
        aml_int(0xffffffff), NULL), aml_index(addr, aml_int(0))));
    aml_append(method, aml_store(aml_shiftright(aml_name("VGIA"),
        aml_int(32), NULL), aml_index(addr, aml_int(1))));
    aml_append(method, aml_return(addr));

    aml_append(dev, method);
    aml_append(scope, dev);
    aml_append(ssdt, scope);

    /* attach an ACPI notify */
    method = aml_method("\\_GPE._E05", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_notify(aml_name("\\_SB.VGEN"), aml_int(0x80)));
    aml_append(ssdt, method);

    g_array_append_vals(table_data, ssdt->buf->data, ssdt->buf->len);

    /* Allocate guest memory for the Address fw_cfg blob */
    bios_linker_loader_alloc(linker, VMGENID_ADDR_FW_CFG_FILE, s->vgia, 0,
                             false /* high memory */);
    /* Allocate guest memory for the GUID fw_cfg blob and return address */
    bios_linker_loader_alloc_ret_addr(linker, VMGENID_GUID_FW_CFG_FILE, guid,
                                      0, false, VMGENID_ADDR_FW_CFG_FILE);
    /* Patch address of GUID fw_cfg blob into the AML */
    bios_linker_loader_add_pointer(linker,
        ACPI_BUILD_TABLE_FILE, vgia_offset, sizeof(uint64_t),
        VMGENID_GUID_FW_CFG_FILE, 0);

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
    /* Create a read-only fw_cfg file for GUID */
    fw_cfg_add_file(s, VMGENID_GUID_FW_CFG_FILE, vms->guid.data,
        sizeof(vms->guid.data));
    /* Create a read-write fw_cfg file for Address */
    fw_cfg_add_file_callback(s, VMGENID_ADDR_FW_CFG_FILE, NULL, NULL,
                             vms->vgia->data, sizeof(uint64_t), false);
}

static void vmgenid_notify_guest(VmGenIdState *s)
{
    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
    if (obj) {
        /* Send _GPE.E00 event */
        acpi_send_event(DEVICE(obj), ACPI_VMGENID_CHANGE_STATUS);
    }
}

static void vmgenid_set_guid(Object *obj, const char *value, Error **errp)
{
    VmGenIdState *s = VMGENID(obj);
    uint64_t *addr;

    if (!strncmp(value, "auto", 4)) {
        qemu_uuid_generate(&s->guid);
    } else if (qemu_uuid_parse(value, &s->guid) < 0) {
        error_setg(errp, "'%s." VMGENID_GUID
                   "': Failed to parse GUID string: %s",
                   object_get_typename(OBJECT(s)),
                   value);
        return;
    }
    /* Find the guest address where the GUID is located and fill it in */
    addr = &g_array_index(s->vgia, uint64_t, 0);
    if (addr) {
        cpu_physical_memory_write(*addr, s->guid.data, 16);
    }

    /* Send the ACPI notify */
    vmgenid_notify_guest(s);
}

static void vmgenid_initfn(Object *obj)
{
    VmGenIdState *s = VMGENID(obj);

    object_property_add_str(obj, VMGENID_GUID, NULL, vmgenid_set_guid, NULL);

    s->vgia = g_array_new(false, true, sizeof(uint64_t));
    g_array_set_size(s->vgia, 1);
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
    info->guid = qemu_uuid_unparse_strdup(&vdev->guid);
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
