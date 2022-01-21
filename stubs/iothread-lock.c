#include "qemu/osdep.h"
#include "qemu/main-loop.h"

bool qemu_mutex_iothread_locked(void)
{
    return false;
}

bool qemu_in_main_thread(void)
{
    return qemu_get_current_aio_context() == qemu_get_aio_context();
}

void qemu_mutex_lock_iothread_impl(const char *file, int line)
{
}

void qemu_mutex_unlock_iothread(void)
{
}
