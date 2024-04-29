#include "qemu/osdep.h"
#include "migration/vmstate.h"

int vmstate_register_with_alias_id(VMStateIf *obj,
                                   uint32_t instance_id,
                                   const VMStateDescription *vmsd,
                                   void *base, int alias_id,
                                   int required_for_version,
                                   const char *instance_name,
                                   Error **errp)
{
    return 0;
}

void vmstate_unregister(VMStateIf *obj,
                        const VMStateDescription *vmsd,
                        void *opaque)
{
}

void vmstate_unregister_named(const char *vmsd_name,
                              const char *instance_name,
                              int instance_id)
{
}

bool vmstate_check_only_migratable(const VMStateDescription *vmsd)
{
    return true;
}
