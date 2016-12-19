#ifndef ACPI_VMGENID_H
#define ACPI_VMGENID_H

#include "hw/acpi/bios-linker-loader.h"
#include "hw/sysbus.h"
#include "qemu/uuid.h"

#define VMGENID_DEVICE           "vmgenid"
#define VMGENID_GUID             "guid"
#define VMGENID_CHANGED          "changed"
#define VMGENID_FW_CFG_FILE      "etc/vmgenid"

Object *find_vmgenid_dev(Error **errp);
void vmgenid_add_fw_cfg(FWCfgState *s);
void vmgenid_build_acpi(GArray *table_offsets, GArray *table_data,
                       BIOSLinker *linker);

#define VMGENID(obj) OBJECT_CHECK(VmGenIdState, (obj), VMGENID_DEVICE)

typedef struct VmGenIdState {
    SysBusDevice parent_obj;
    QemuUUID guid;
    bool is_changed;
} VmGenIdState;

#endif
