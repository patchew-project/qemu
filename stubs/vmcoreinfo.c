#include "qemu/osdep.h"

#include "hw/acpi/vmcoreinfo.h"

bool vmcoreinfo_get(VMCoreInfoState *vis, uint64_t *paddr, uint32_t *size,
                    Error **errp)
{
    return false;
}
