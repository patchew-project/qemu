#include "qemu/osdep.h"
#include "sysemu/cpu-timers.h"
#include "qemu/main-loop.h"

int64_t icount_get(void)
{
    abort();
}

int64_t icount_get_raw(void)
{
    abort();
}

int icount_enabled(void)
{
    return 0;
}

void qemu_timer_notify_cb(void *opaque, QEMUClockType type)
{
    qemu_notify_event();
}
