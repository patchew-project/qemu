#include "qemu/osdep.h"
#include "qemu/main-loop.h"

bool qemu_bql_locked(void)
{
    return false;
}

void qemu_bql_lock_impl(const char *file, int line)
{
}

void qemu_bql_unlock(void)
{
}
