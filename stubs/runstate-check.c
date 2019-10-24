#include "qemu/osdep.h"

#include "sysemu/runstate.h"

#pragma weak runstate_check

bool runstate_check(RunState state)
{
    return state == RUN_STATE_PRELAUNCH;
}
