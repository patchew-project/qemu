/*
 *  Virtual Machine Generation ID Device
 *
 *  Copyright (C) 2017 Skyport Systems.
 *
 *  Author: Ben Warren <ben@skyportsystems.com>
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

void vmgenid_build_acpi(GArray *table_data, GArray *guid, BIOSLinker *linker)
{
    Object *obj;
    VmGenIdState *s;
    Aml *ssdt, *dev, *scope, *method, *addr, *if_ctx;
    uint32_t vgia_offset;

    obj = find_vmgenid_dev(NULL);
    assert(obj);
    s = VMGENID(obj);

    /* Fill in the GUID values */
    if (guid->len != VMGENID_FW_CFG_SIZE) {
        g_array_set_size(guid, VMGENID_FW_CFG_SIZE);
    }
    g_array_insert_vals(guid, VMGENID_GUID_OFFSET, s->guid.data, 16);

    /* Put this in a separate SSDT table */
    ssdt = init_aml_allocator();

    /* Reserve space for header */
    acpi_data_push(ssdt->buf, sizeof(AcpiTableHeader));

    /* Storage for the GUID address */
    vgia_offset = table_data->len +
        build_append_named_dword(ssdt->buf, "VGIA");
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

    aml_append(method, aml_store(aml_add(aml_name("VGIA"),
                                         aml_int(VMGENID_GUID_OFFSET), NULL),
                                 aml_index(addr, aml_int(0))));
    aml_append(method, aml_store(aml_int(0), aml_index(addr, aml_int(1))));
    aml_append(method, aml_return(addr));

    aml_append(dev, method);
    aml_append(scope, dev);
    aml_append(ssdt, scope);

    /* attach an ACPI notify */
    method = aml_method("\\_GPE._E05", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_notify(aml_name("\\_SB.VGEN"), aml_int(0x80)));
    aml_append(ssdt, method);

    g_array_append_vals(table_data, ssdt->buf->data, ssdt->buf->len);

    /* Allocate guest memory for the Data fw_cfg blob */
    bios_linker_loader_alloc(linker, VMGENID_GUID_FW_CFG_FILE, guid, 4096,
                             false /* page boundary, high memory */);

    /* Patch address of GUID fw_cfg blob into the ADDR fw_cfg blob */
    bios_linker_loader_add_pointer(linker,
        VMGENID_ADDR_FW_CFG_FILE, 0, sizeof(uint32_t),
        VMGENID_GUID_FW_CFG_FILE, 0, true);

    /* Patch address of GUID fw_cfg blob into the AML */
    bios_linker_loader_add_pointer(linker,
        ACPI_BUILD_TABLE_FILE, vgia_offset, sizeof(uint32_t),
        VMGENID_GUID_FW_CFG_FILE, 0, false);

    build_header(linker, table_data,
        (void *)(table_data->data + table_data->len - ssdt->buf->len),
        "SSDT", ssdt->buf->len, 1, NULL, "VMGENID");
    free_aml_allocator();
}

void vmgenid_add_fw_cfg(FWCfgState *s, GArray *guid)
{
    Object *obj = find_vmgenid_dev(NULL);
    assert(obj);
    VmGenIdState *vms = VMGENID(obj);

    /* Create a read-only fw_cfg file for GUID */
    fw_cfg_add_file(s, VMGENID_GUID_FW_CFG_FILE, guid->data,
                    VMGENID_FW_CFG_SIZE);
    /* Create a read-write fw_cfg file for Address */
    fw_cfg_add_file_callback(s, VMGENID_ADDR_FW_CFG_FILE, NULL, NULL,
                             vms->vgia_le, sizeof(uint32_t), false);
}

static void vmgenid_update_guest(VmGenIdState *s)
{
    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
    uint32_t vgia;

    if (obj) {
        /* Write the GUID to guest memory */
        memcpy(&vgia, s->vgia_le, sizeof(vgia));
        vgia = le32_to_cpu(vgia);
        if (vgia) {
            cpu_physical_memory_write(vgia + VMGENID_GUID_OFFSET,
                                      s->guid.data, sizeof(s->guid.data));
            /* Send _GPE.E05 event */
            acpi_send_event(DEVICE(obj), ACPI_VMGENID_CHANGE_STATUS);
        }
    }
}

static void vmgenid_set_guid(Object *obj, const char *value, Error **errp)
{
    VmGenIdState *s = VMGENID(obj);

    if (!strncmp(value, "auto", 4)) {
        qemu_uuid_generate(&s->guid);
    } else if (qemu_uuid_parse(value, &s->guid) < 0) {
        error_setg(errp, "'%s. %s': Failed to parse GUID string: %s",
                   object_get_typename(OBJECT(s)), VMGENID_GUID, value);
        return;
    }
    /* QemuUUID has the first three words as big-endian, and expect that any
     * GUIDs passed in will always be BE.  The guest, however will expect
     * the fields to be little-endian, so store that way internally.  Make
     * sure to swap back whenever reporting via monitor */
    qemu_uuid_bswap(&s->guid);

    /* Send the ACPI notify */
    vmgenid_update_guest(s);
}

/* After restoring an image, we need to update the guest memory and notify
 * it of a potential change to VM Generation ID */
static int vmgenid_post_load(void *opaque, int version_id)
{
    VmGenIdState *s = opaque;
    vmgenid_update_guest(s);
    return 0;
}

static const VMStateDescription vmstate_vmgenid = {
    .name = "vmgenid",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = vmgenid_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(vgia_le, VmGenIdState, sizeof(uint32_t)),
        VMSTATE_END_OF_LIST()
    },
};

static void vmgenid_initfn(Object *obj)
{
    object_property_add_str(obj, VMGENID_GUID, NULL, vmgenid_set_guid, NULL);
}

static void vmgenid_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_vmgenid;
}

static const TypeInfo vmgenid_device_info = {
    .name          = VMGENID_DEVICE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VmGenIdState),
    .instance_init = vmgenid_initfn,
    .class_init    = vmgenid_device_class_init,
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
    QemuUUID guid;
    Object *obj = find_vmgenid_dev(errp);

    if (!obj) {
        return NULL;
    }
    vdev = VMGENID(obj);
    /* Convert GUID back to big-endian before displaying */
    memcpy(&guid, &vdev->guid, sizeof(guid));
    qemu_uuid_bswap(&guid);

    info = g_malloc0(sizeof(*info));
    info->guid = qemu_uuid_unparse_strdup(&guid);
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
