#ifndef ACPI_VMCOREINFO_H
#define ACPI_VMCOREINFO_H

#include "hw/acpi/bios-linker-loader.h"
#include "hw/qdev.h"

#define VMCOREINFO_DEVICE           "vmcoreinfo"
#define VMCOREINFO_FW_CFG_FILE      "etc/vmcoreinfo"
#define VMCOREINFO_ADDR_FW_CFG_FILE "etc/vmcoreinfo-addr"

#define VMCOREINFO_FW_CFG_SIZE      4096 /* Occupy a page of memory */
#define VMCOREINFO_OFFSET           40   /* allow space for
                                          * OVMF SDT Header Probe Supressor
                                          */

#define VMCOREINFO(obj) OBJECT_CHECK(VmcoreinfoState, (obj), VMCOREINFO_DEVICE)

typedef struct VmcoreinfoState {
    DeviceClass parent_obj;
    uint8_t vmcoreinfo_addr_le[8];   /* Address of memory region */
    bool write_pointer_available;
} VmcoreinfoState;

/* returns NULL unless there is exactly one device */
static inline Object *find_vmcoreinfo_dev(void)
{
    return object_resolve_path_type("", VMCOREINFO_DEVICE, NULL);
}

void vmcoreinfo_build_acpi(VmcoreinfoState *vis, GArray *table_data,
                           GArray *vmci, BIOSLinker *linker);
void vmcoreinfo_add_fw_cfg(VmcoreinfoState *vis, FWCfgState *s, GArray *vmci);
bool vmcoreinfo_get(VmcoreinfoState *vis, uint64_t *paddr, uint32_t *size,
                    Error **errp);

#endif
