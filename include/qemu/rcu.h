#ifndef QEMU_RCU_H
#define QEMU_RCU_H

/*
 * urcu-mb.h
 *
 * Userspace RCU header with explicit memory barrier.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 * IBM's contributions to this file may be relicensed under LGPLv2 or later.
 */


#include "qemu/thread.h"
#include "qemu/queue.h"
#include "qemu/atomic.h"
#include "qemu/notify.h"
#include "qemu/sys_membarrier.h"
#include "qemu/coroutine-tls.h"

/*
 * Important !
 *
 * Each thread containing read-side critical sections must be registered
 * with rcu_register_thread() before calling rcu_read_lock().
 * rcu_unregister_thread() should be called before the thread exits.
 */

#ifdef DEBUG_RCU
#define rcu_assert(args...)    assert(args)
#else
#define rcu_assert(args...)
#endif

/*
 * Global quiescent period counter with low-order bits unused.
 * Using a int rather than a char to eliminate false register dependencies
 * causing stalls on some architectures.
 */
extern unsigned long rcu_gp_ctr;

extern QemuEvent rcu_gp_event;

struct rcu_reader_data {
    /* Data used by both reader and synchronize_rcu() */
    unsigned long ctr;
    bool waiting;

    /* Data used by reader only */
    unsigned depth;

    /* Data used for registry, protected by rcu_registry_lock */
    QLIST_ENTRY(rcu_reader_data) node;

    /*
     * NotifierList used to force an RCU grace period.  Accessed under
     * rcu_registry_lock.  Note that the notifier is called _outside_
     * the thread!
     */
    NotifierList force_rcu;
};

QEMU_DECLARE_CO_TLS(struct rcu_reader_data, rcu_reader)

void rcu_read_lock(void);
void rcu_read_unlock(void);
void synchronize_rcu(void);

/*
 * Reader thread registration.
 */
void rcu_register_thread(void);
void rcu_unregister_thread(void);

/*
 * Support for fork().  fork() support is enabled at startup.
 */
void rcu_enable_atfork(void);
void rcu_disable_atfork(void);

struct rcu_head;
typedef void RCUCBFunc(struct rcu_head *head);

struct rcu_head {
    struct rcu_head *next;
    RCUCBFunc *func;
};

void call_rcu1(struct rcu_head *head, RCUCBFunc *func);
void drain_call_rcu(void);

/* The operands of the minus operator must have the same type,
 * which must be the one that we specify in the cast.
 */
#define call_rcu(head, func, field)                                      \
    call_rcu1(({                                                         \
         char __attribute__((unused))                                    \
            offset_must_be_zero[-offsetof(typeof(*(head)), field)],      \
            func_type_invalid = (func) - (void (*)(typeof(head)))(func); \
         &(head)->field;                                                 \
      }),                                                                \
      (RCUCBFunc *)(func))

#define g_free_rcu(obj, field) \
    call_rcu1(({                                                         \
        char __attribute__((unused))                                     \
            offset_must_be_zero[-offsetof(typeof(*(obj)), field)];       \
        &(obj)->field;                                                   \
      }),                                                                \
      (RCUCBFunc *)g_free);

typedef void RCUReadAuto;
static inline RCUReadAuto *rcu_read_auto_lock(void)
{
    rcu_read_lock();
    /* Anything non-NULL causes the cleanup function to be called */
    return (void *)(uintptr_t)0x1;
}

static inline void rcu_read_auto_unlock(RCUReadAuto *r)
{
    rcu_read_unlock();
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(RCUReadAuto, rcu_read_auto_unlock)

#define WITH_RCU_READ_LOCK_GUARD() \
    WITH_RCU_READ_LOCK_GUARD_(glue(_rcu_read_auto, __COUNTER__))

#define WITH_RCU_READ_LOCK_GUARD_(var) \
    for (g_autoptr(RCUReadAuto) var = rcu_read_auto_lock(); \
        (var); rcu_read_auto_unlock(var), (var) = NULL)

#define RCU_READ_LOCK_GUARD() \
    g_autoptr(RCUReadAuto) _rcu_read_auto __attribute__((unused)) = rcu_read_auto_lock()

/*
 * Force-RCU notifiers tell readers that they should exit their
 * read-side critical section.
 */
void rcu_add_force_rcu_notifier(Notifier *n);
void rcu_remove_force_rcu_notifier(Notifier *n);

#endif /* QEMU_RCU_H */
