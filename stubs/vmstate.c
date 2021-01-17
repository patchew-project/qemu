#include "qemu/osdep.h"
#include "migration/vmstate.h"

#if defined(CONFIG_USER_ONLY)
const VMStateDescription vmstate_user_mode_cpu_dummy = {
    .name = "cpu_common_user",
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    },
};
#endif

const VMStateDescription vmstate_no_state_to_migrate = {
    .name = "empty-state",
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

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
