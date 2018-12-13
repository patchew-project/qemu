#include "qemu/osdep.h"
#include "qom/cpu.h"

void cpu_mutex_lock_impl(CPUState *cpu, const char *file, int line)
{
/* coverity gets confused by the indirect function call */
#ifdef __COVERITY__
    qemu_mutex_lock_impl(&cpu->lock, file, line);
#else
    QemuMutexLockFunc f = atomic_read(&qemu_mutex_lock_func);
    f(&cpu->lock, file, line);
#endif
}

void cpu_mutex_unlock_impl(CPUState *cpu, const char *file, int line)
{
    qemu_mutex_unlock_impl(&cpu->lock, file, line);
}

bool cpu_mutex_locked(const CPUState *cpu)
{
    return true;
}

bool no_cpu_mutex_locked(void)
{
    return true;
}
