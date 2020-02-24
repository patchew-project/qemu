#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "migration/misc.h"

const VMStateDescription vmstate_dummy = {};
const VMStateInfo vmstate_info_timer;

int vmstate_register_with_alias_id(VMStateIf *obj,
                                   uint32_t instance_id,
                                   const VMStateDescription *vmsd,
                                   void *base, int alias_id,
                                   int required_for_version,
                                   Error **errp)
{
    return 0;
}

void vmstate_unregister(VMStateIf *obj,
                        const VMStateDescription *vmsd,
                        void *opaque)
{
}

bool vmstate_check_only_migratable(const VMStateDescription *vmsd)
{
    return true;
}

void vmstate_register_ram(MemoryRegion *mr, DeviceState *dev)
{
}

void vmstate_unregister_ram(MemoryRegion *mr, DeviceState *dev)
{
}

void vmstate_register_ram_global(MemoryRegion *mr)
{
}

bool migration_is_idle(void)
{
    return true;
}
