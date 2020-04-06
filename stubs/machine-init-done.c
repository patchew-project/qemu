#include "qemu/osdep.h"
#include "sysemu/sysemu.h"

bool machine_init_done = true;

static NotifierList machine_init_done_notifiers =
    NOTIFIER_LIST_INITIALIZER(machine_init_done_notifiers);

void qemu_add_machine_init_done_notifier(Notifier *notify)
{
}

void qemu_remove_machine_init_done_notifier(Notifier *notify)
{
}

void qemu_run_machine_init_done_notifiers(void)
{
    notifier_list_notify(&machine_init_done_notifiers, NULL);
}
