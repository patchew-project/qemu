#include "qemu/osdep.h"
#include "sysemu/sysemu.h"

bool machine_init_done = true;

NotifierList machine_init_done_notifiers =
    NOTIFIER_LIST_INITIALIZER(machine_init_done_notifiers);
