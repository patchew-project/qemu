#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"

VMChangeStateEntry *qemu_add_vm_change_state_handler(VMChangeStateHandler *cb,
                                                     void *opaque)
{
    return g_malloc0(1);
}

void qemu_del_vm_change_state_handler(VMChangeStateEntry *e)
{
    g_free(e);
}
