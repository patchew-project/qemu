#ifndef ACPI_VMGENID_H
#define ACPI_VMGENID_H

#include "hw/acpi/bios-linker-loader.h"
#include "hw/sysbus.h"
#include "qemu/uuid.h"

#define VMGENID_DEVICE           "vmgenid"
#define VMGENID_GUID             "guid"
#define VMGENID_GUID_FW_CFG_FILE      "etc/vmgenid"
#define VMGENID_ADDR_FW_CFG_FILE      "etc/vmgenid_addr"

#define VMGENID_FW_CFG_SIZE      4096 /* Occupy a page of memory */
#define VMGENID_GUID_OFFSET      40   /* allow space for
                                       * OVMF SDT Header Probe Supressor */

void vmgenid_add_fw_cfg(FWCfgState *s, GArray *guid);
void vmgenid_build_acpi(GArray *table_data, GArray *guid, BIOSLinker *linker);

#define VMGENID(obj) OBJECT_CHECK(VmGenIdState, (obj), VMGENID_DEVICE)

typedef struct VmGenIdState {
    SysBusDevice parent_obj;
    QemuUUID guid;
    uint8_t vgia_le[4];
} VmGenIdState;

static Object *find_vmgenid_dev(Error **errp)
{
    Object *obj = object_resolve_path_type("", VMGENID_DEVICE, NULL);
    if (!obj && errp) {
        error_setg(errp, "%s is not found", VMGENID_DEVICE);
    }
    return obj;
}

#endif
