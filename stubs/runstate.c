#include "qemu/osdep.h"

#include "sysemu/runstate.h"
bool runstate_check(RunState state)
{
    return state == RUN_STATE_PRELAUNCH;
}

void qemu_system_shutdown_request(ShutdownCause reason)
{
    return;
}
