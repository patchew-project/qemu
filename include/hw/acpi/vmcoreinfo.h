#ifndef ACPI_VMCOREINFO_H
#define ACPI_VMCOREINFO_H

#include "hw/acpi/bios-linker-loader.h"
#include "hw/qdev.h"

#define VMCOREINFO_DEVICE           "vmcoreinfo"
#define VMCOREINFO_FW_CFG_FILE      "etc/vmcoreinfo"
#define VMCOREINFO_ADDR_FW_CFG_FILE "etc/vmcoreinfo-addr"

/* Occupy a page of memory */
#define VMCOREINFO_FW_CFG_SIZE      4096

/* allow space for OVMF SDT Header Probe Supressor */
#define VMCOREINFO_OFFSET           sizeof(AcpiTableHeader)

#define VMCOREINFO(obj) OBJECT_CHECK(VMCoreInfoState, (obj), VMCOREINFO_DEVICE)

typedef struct VMCoreInfoState {
    DeviceClass parent_obj;
    uint8_t vmcoreinfo_addr_le[8];   /* Address of memory region */
    bool write_pointer_available;
} VMCoreInfoState;

/* returns NULL unless there is exactly one device */
static inline Object *find_vmcoreinfo_dev(void)
{
    return object_resolve_path_type("", VMCOREINFO_DEVICE, NULL);
}

void vmcoreinfo_build_acpi(VMCoreInfoState *vis, GArray *table_data,
                           GArray *vmci, BIOSLinker *linker);
void vmcoreinfo_add_fw_cfg(VMCoreInfoState *vis, FWCfgState *s, GArray *vmci);
bool vmcoreinfo_get(VMCoreInfoState *vis, uint64_t *paddr, uint32_t *size,
                    Error **errp);

#endif
