/*
 *  Linux process related syscalls
 *  Copyright (c) 2003 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


SYSCALL_IMPL(exit)
{
    CPUState *cpu = ENV_GET_CPU(cpu_env);
    int status = arg1;

    /*
     * In old applications this may be used to implement _exit(2).
     * However in threaded applictions it is used for thread termination,
     * and _exit_group is used for application termination.
     * Do thread termination if we have more then one thread.
     */
    if (block_signals()) {
        return -TARGET_ERESTARTSYS;
    }

    cpu_list_lock();

    if (CPU_NEXT(first_cpu)) {
        TaskState *ts;

        /* Remove the CPU from the list.  */
        QTAILQ_REMOVE_RCU(&cpus, cpu, node);

        cpu_list_unlock();

        ts = cpu->opaque;
        if (ts->child_tidptr) {
            put_user_u32(0, ts->child_tidptr);
            sys_futex(g2h(ts->child_tidptr), FUTEX_WAKE, INT_MAX,
                      NULL, NULL, 0);
        }
        thread_cpu = NULL;
        object_unref(OBJECT(cpu));
        g_free(ts);
        rcu_unregister_thread();
        pthread_exit(NULL);
    }

    cpu_list_unlock();
    preexit_cleanup(cpu_env, status);
    _exit(status);
}
