#ifndef ACPI_VMGENID_H
#define ACPI_VMGENID_H

#include "hw/acpi/bios-linker-loader.h"
#include "hw/qdev-core.h"
#include "qemu/uuid.h"
#include "qom/object.h"

#define TYPE_VMGENID                  "vmgenid"
#define VMGENID_GUID                  "guid"
#define VMGENID_PAGE_SIZE             "page-size"
#define VMGENID_GEN_CTR               "genctr"
#define VMGENID_DATA_FW_CFG_FILE      "etc/vmgenid_data"
#define VMGENID_ADDR_FW_CFG_FILE      "etc/vmgenid_addr"

#define VMGENID_DEFAULT_FW_PAGE_SIZE  4096 /* Assume 4K page by default */
#define VMGENID_GUID_OFFSET           40   /* allow space for OVMF SDT Header
                                            * Probe Supressor
                                            */

OBJECT_DECLARE_SIMPLE_TYPE(VmGenIdState, VMGENID)

struct VmGenIdState {
    DeviceState parent_obj;
    QemuUUID guid;                /* The 128-bit GUID seen by the guest */
    uint32_t gen_ctr;             /* A 32bit generation counter meant to
                                   * be exposed in the guest user-space
                                   */
    bool has_gen_ctr;             /* Backwards compatibility: Only update a
                                   * guest when generation current present
                                   */
    uint8_t vmgenid_addr_le[8];   /* Address of the GUID (little-endian) */
    uint32_t page_size;           /* Page size to use as a the allocation unit */
};

/* returns NULL unless there is exactly one device */
static inline Object *find_vmgenid_dev(void)
{
    return object_resolve_path_type("", TYPE_VMGENID, NULL);
}

void vmgenid_build_acpi(VmGenIdState *vms, GArray *table_data, GArray *guid,
                        BIOSLinker *linker, const char *oem_id);
void vmgenid_add_fw_cfg(VmGenIdState *vms, FWCfgState *s, GArray *guid);

#endif
