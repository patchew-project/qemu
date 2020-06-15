#include "qemu/osdep.h"
#include "sysemu/cpu-timers.h"

int64_t icount_get(void)
{
    abort();
}

int64_t icount_get_raw(void)
{
    abort();
}

void icount_configure(QemuOpts *opts, Error **errp)
{
    abort();
}

int icount_enabled(void)
{
    return 0;
}
