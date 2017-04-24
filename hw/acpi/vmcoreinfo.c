/*
 *  Virtual Machine coreinfo device
 *  (based on Virtual Machine Generation ID Device)
 *
 *  Copyright (C) 2017 Red Hat, Inc.
 *  Copyright (C) 2017 Skyport Systems.
 *
 *  Authors: Marc-André Lureau <marcandre.lureau@redhat.com>
 *           Ben Warren <ben@skyportsystems.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/vmcoreinfo.h"
#include "hw/nvram/fw_cfg.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"

void vmcoreinfo_build_acpi(VmcoreinfoState *vis, GArray *table_data,
                           GArray *vmci, BIOSLinker *linker)
{
    Aml *ssdt, *dev, *scope, *method, *addr, *if_ctx;
    uint32_t vgia_offset;

    g_array_set_size(vmci, VMCOREINFO_FW_CFG_SIZE);

    /* Put this in a separate SSDT table */
    ssdt = init_aml_allocator();

    /* Reserve space for header */
    acpi_data_push(ssdt->buf, sizeof(AcpiTableHeader));

    /* Storage address */
    vgia_offset = table_data->len +
        build_append_named_dword(ssdt->buf, "VCIA");
    scope = aml_scope("\\_SB");
    dev = aml_device("VMCI");
    aml_append(dev, aml_name_decl("_HID", aml_string("QEMUVMCI")));

    /* Simple status method to check that address is linked and non-zero */
    method = aml_method("_STA", 0, AML_NOTSERIALIZED);
    addr = aml_local(0);
    aml_append(method, aml_store(aml_int(0xf), addr));
    if_ctx = aml_if(aml_equal(aml_name("VCIA"), aml_int(0)));
    aml_append(if_ctx, aml_store(aml_int(0), addr));
    aml_append(method, if_ctx);
    aml_append(method, aml_return(addr));
    aml_append(dev, method);

    /* the ADDR method returns two 32-bit words representing the lower and
     * upper halves of the physical address of the vmcoreinfo area
     */
    method = aml_method("ADDR", 0, AML_NOTSERIALIZED);

    addr = aml_local(0);
    aml_append(method, aml_store(aml_package(2), addr));

    aml_append(method, aml_store(aml_add(aml_name("VCIA"),
                                         aml_int(VMCOREINFO_OFFSET), NULL),
                                 aml_index(addr, aml_int(0))));
    aml_append(method, aml_store(aml_int(0), aml_index(addr, aml_int(1))));
    aml_append(method, aml_return(addr));

    aml_append(dev, method);
    aml_append(scope, dev);
    aml_append(ssdt, scope);

    g_array_append_vals(table_data, ssdt->buf->data, ssdt->buf->len);

    /* Allocate guest memory */
    bios_linker_loader_alloc(linker, VMCOREINFO_FW_CFG_FILE, vmci, 4096,
                             false /* page boundary, high memory */);

    /* Patch address of vmcoreinfo fw_cfg blob into the ADDR fw_cfg
     * blob so QEMU can read the info from there.  The address is
     * expected to be < 4GB, but write 64 bits anyway.
     * The address that is patched in is offset in order to implement
     * the "OVMF SDT Header probe suppressor"
     * see docs/specs/vmcoreinfo.txt for more details.
     */
    bios_linker_loader_write_pointer(linker,
        VMCOREINFO_ADDR_FW_CFG_FILE, 0, sizeof(uint64_t),
        VMCOREINFO_FW_CFG_FILE, VMCOREINFO_OFFSET);

    /* Patch address of vmcoreinfo into the AML so OSPM can retrieve
     * and read it.  Note that while we provide storage for 64 bits, only
     * the least-signficant 32 get patched into AML.
     */
    bios_linker_loader_add_pointer(linker,
        ACPI_BUILD_TABLE_FILE, vgia_offset, sizeof(uint32_t),
        VMCOREINFO_FW_CFG_FILE, 0);

    build_header(linker, table_data,
        (void *)(table_data->data + table_data->len - ssdt->buf->len),
        "SSDT", ssdt->buf->len, 1, NULL, "VMCOREIN");
    free_aml_allocator();
}

void vmcoreinfo_add_fw_cfg(VmcoreinfoState *vis, FWCfgState *s, GArray *vmci)
{
    /* Create a read-only fw_cfg file for vmcoreinfo allocation */
    /* XXX: linker could learn to allocate without backing fw_cfg? */
    fw_cfg_add_file(s, VMCOREINFO_FW_CFG_FILE, vmci->data,
                    VMCOREINFO_FW_CFG_SIZE);
    /* Create a read-write fw_cfg file for Address */
    fw_cfg_add_file_callback(s, VMCOREINFO_ADDR_FW_CFG_FILE, NULL, NULL,
                             vis->vmcoreinfo_addr_le,
                             ARRAY_SIZE(vis->vmcoreinfo_addr_le), false);
}

bool vmcoreinfo_get(VmcoreinfoState *vis,
                    uint64_t *paddr, uint32_t *size,
                    Error **errp)
{
    uint32_t vmcoreinfo_addr;
    uint32_t version;

    assert(vis);
    assert(paddr);
    assert(size);

    memcpy(&vmcoreinfo_addr, vis->vmcoreinfo_addr_le, sizeof(vmcoreinfo_addr));
    vmcoreinfo_addr = le32_to_cpu(vmcoreinfo_addr);
    if (!vmcoreinfo_addr) {
        error_setg(errp, "BIOS has not yet written the address of %s",
                   VMCOREINFO_DEVICE);
        return false;
    }

    cpu_physical_memory_read(vmcoreinfo_addr, &version, sizeof(version));
    if (version != 0) {
        error_setg(errp, "Unknown %s memory version", VMCOREINFO_DEVICE);
        return false;
    }

    cpu_physical_memory_read(vmcoreinfo_addr + 4, paddr, sizeof(paddr));
    *paddr = le64_to_cpu(*paddr);
    cpu_physical_memory_read(vmcoreinfo_addr + 12, size, sizeof(size));
    *size = le32_to_cpu(*size);

    return true;
}

static const VMStateDescription vmstate_vmcoreinfo = {
    .name = "vmcoreinfo",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(vmcoreinfo_addr_le, VmcoreinfoState, sizeof(uint64_t)),
        VMSTATE_END_OF_LIST()
    },
};

static Property vmcoreinfo_properties[] = {
    DEFINE_PROP_BOOL("x-write-pointer-available", VmcoreinfoState,
                     write_pointer_available, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void vmcoreinfo_realize(DeviceState *dev, Error **errp)
{
    VmcoreinfoState *vms = VMCOREINFO(dev);

    if (!vms->write_pointer_available) {
        error_setg(errp, "%s requires DMA write support in fw_cfg, "
                   "which this machine type does not provide",
                   VMCOREINFO_DEVICE);
        return;
    }

    /* Given that this function is executing, there is at least one VMCOREINFO
     * device. Check if there are several.
     */
    if (!find_vmcoreinfo_dev()) {
        error_setg(errp, "at most one %s device is permitted",
                   VMCOREINFO_DEVICE);
        return;
    }
}

static void vmcoreinfo_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_vmcoreinfo;
    dc->realize = vmcoreinfo_realize;
    dc->hotpluggable = false;
    dc->props = vmcoreinfo_properties;
}

static const TypeInfo vmcoreinfo_device_info = {
    .name          = VMCOREINFO_DEVICE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(VmcoreinfoState),
    .class_init    = vmcoreinfo_device_class_init,
};

static void vmcoreinfo_register_types(void)
{
    type_register_static(&vmcoreinfo_device_info);
}

type_init(vmcoreinfo_register_types)
