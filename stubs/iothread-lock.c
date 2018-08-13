#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/main-loop.h"

bool qemu_mutex_iothread_locked(void)
{
    return true;
}

#ifdef CONFIG_SYNC_PROFILER
void do_qemu_mutex_lock_iothread(const char *file, int line)
{
}
#else
void do_qemu_mutex_lock_iothread(void)
{
}
#endif

void qemu_mutex_unlock_iothread(void)
{
}
