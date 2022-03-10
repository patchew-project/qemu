/*
 * QEMU coroutine implementation
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi    <stefanha@linux.vnet.ibm.com>
 *  Kevin Wolf         <kwolf@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QEMU_COROUTINE_H
#define QEMU_COROUTINE_H

#include "qemu/queue.h"
#include "qemu/timer.h"

typedef enum {
    COROUTINE_YIELD = 1,
    COROUTINE_TERMINATE = 2,
    COROUTINE_ENTER = 3,
    COROUTINE_CONTINUE = 4,
} CoroutineAction;

typedef CoroutineAction CoroutineImpl(void *opaque);

typedef struct {
    CoroutineImpl *caller_func;
    void *caller_frame;
} CoroutineFrame;

/**
 * Coroutines are a mechanism for stack switching and can be used for
 * cooperative userspace threading.  These functions provide a simple but
 * useful flavor of coroutines that is suitable for writing sequential code,
 * rather than callbacks, for operations that need to give up control while
 * waiting for events to complete.
 *
 * These functions are re-entrant and may be used outside the global mutex.
 */

/**
 * Mark a function that executes in coroutine context
 *
 * Functions that execute in coroutine context cannot be called directly from
 * normal functions.  In the future it would be nice to enable compiler or
 * static checker support for catching such errors.  This annotation might make
 * it possible and in the meantime it serves as documentation.
 *
 * For example:
 *
 *   static void coroutine_fn foo(void) {
 *       ....
 *   }
 */
#define coroutine_fn
#define coroutine_only_fn

typedef struct Coroutine Coroutine;

/**
 * Coroutine entry point
 *
 * When the coroutine is entered for the first time, opaque is passed in as an
 * argument.
 *
 * When this function returns, the coroutine is destroyed automatically and
 * execution continues in the caller who last entered the coroutine.
 */
typedef CoroutineAction CoroutineEntry(void *opaque);

/**
 * Create a new coroutine
 *
 * Use qemu_coroutine_enter() to actually transfer control to the coroutine.
 * The opaque argument is passed as the argument to the entry point.
 */
Coroutine *qemu_coroutine_create(CoroutineEntry *entry, void *opaque);

/**
 * Transfer control to a coroutine
 */
void qemu_coroutine_enter(Coroutine *coroutine);

/**
 * Transfer control to a coroutine if it's not active (i.e. part of the call
 * stack of the running coroutine). Otherwise, do nothing.
 */
void qemu_coroutine_enter_if_inactive(Coroutine *co);

/**
 * Transfer control to a coroutine and associate it with ctx
 */
void qemu_aio_coroutine_enter(AioContext *ctx, Coroutine *co);

/**
 * Transfer control back to a coroutine's caller
 *
 * This function does not return until the coroutine is re-entered using
 * qemu_coroutine_enter().
 */
CoroutineAction qemu_coroutine_yield(void);

/**
 * Get the AioContext of the given coroutine
 */
AioContext *qemu_coroutine_get_aio_context(Coroutine *co);

/**
 * Get the currently executing coroutine
 */
Coroutine *coroutine_only_fn qemu_coroutine_self(void);

/**
 * Return whether or not currently inside a coroutine
 *
 * This can be used to write functions that work both when in coroutine context
 * and when not in coroutine context.  Note that such functions cannot use the
 * coroutine_fn annotation since they work outside coroutine context.
 */
bool qemu_in_coroutine(void);

/**
 * Return true if the coroutine is currently entered
 *
 * A coroutine is "entered" if it has not yielded from the current
 * qemu_coroutine_enter() call used to run it.  This does not mean that the
 * coroutine is currently executing code since it may have transferred control
 * to another coroutine using qemu_coroutine_enter().
 *
 * When several coroutines enter each other there may be no way to know which
 * ones have already been entered.  In such situations this function can be
 * used to avoid recursively entering coroutines.
 */
bool qemu_coroutine_entered(Coroutine *co);

/**
 * Provides a mutex that can be used to synchronise coroutines
 */
struct CoWaitRecord;
struct CoMutex {
    /* Count of pending lockers; 0 for a free mutex, 1 for an
     * uncontended mutex.
     */
    unsigned locked;

    /* Context that is holding the lock.  Useful to avoid spinning
     * when two coroutines on the same AioContext try to get the lock. :)
     */
    AioContext *ctx;

    /* A queue of waiters.  Elements are added atomically in front of
     * from_push.  to_pop is only populated, and popped from, by whoever
     * is in charge of the next wakeup.  This can be an unlocker or,
     * through the handoff protocol, a locker that is about to go to sleep.
     */
    QSLIST_HEAD(, CoWaitRecord) from_push, to_pop;

    unsigned handoff, sequence;

    Coroutine *holder;
};

/**
 * Initialises a CoMutex. This must be called before any other operation is used
 * on the CoMutex.
 */
void qemu_co_mutex_init(CoMutex *mutex);

/**
 * Locks the mutex. If the lock cannot be taken immediately, control is
 * transferred to the caller of the current coroutine.
 */
CoroutineAction qemu_co_mutex_lock(CoMutex *mutex);

/**
 * Unlocks the mutex and schedules the next coroutine that was waiting for this
 * lock to be run.
 */
CoroutineAction qemu_co_mutex_unlock(CoMutex *mutex);

/**
 * Assert that the current coroutine holds @mutex.
 */
static inline void coroutine_only_fn qemu_co_mutex_assert_locked(CoMutex *mutex)
{
    /*
     * mutex->holder doesn't need any synchronisation if the assertion holds
     * true because the mutex protects it. If it doesn't hold true, we still
     * don't mind if another thread takes or releases mutex behind our back,
     * because the condition will be false no matter whether we read NULL or
     * the pointer for any other coroutine.
     */
    assert(qatomic_read(&mutex->locked) &&
           mutex->holder == qemu_coroutine_self());
}

/**
 * CoQueues are a mechanism to queue coroutines in order to continue executing
 * them later.  They are similar to condition variables, but they need help
 * from an external mutex in order to maintain thread-safety.
 */
typedef struct CoQueue {
    QSIMPLEQ_HEAD(, Coroutine) entries;
} CoQueue;

/**
 * Initialise a CoQueue. This must be called before any other operation is used
 * on the CoQueue.
 */
void qemu_co_queue_init(CoQueue *queue);

/**
 * Adds the current coroutine to the CoQueue and transfers control to the
 * caller of the coroutine.  The mutex is unlocked during the wait and
 * locked again afterwards.
 */
#define qemu_co_queue_wait(queue, lock) \
    qemu_co_queue_wait_impl(queue, QEMU_MAKE_CO_LOCKABLE(lock))
CoroutineAction qemu_co_queue_wait_impl(CoQueue *queue, QemuCoLockable *lock);

/**
 * Removes the next coroutine from the CoQueue, and wake it up.
 * Returns true if a coroutine was removed, false if the queue is empty.
 * OK to run from coroutine and non-coroutine context.
 */
bool qemu_co_queue_next(CoQueue *queue);

/**
 * Empties the CoQueue; all coroutines are woken up.
 * OK to run from coroutine and non-coroutine context.
 */
void qemu_co_queue_restart_all(CoQueue *queue);

/**
 * Removes the next coroutine from the CoQueue, and wake it up.  Unlike
 * qemu_co_queue_next, this function releases the lock during aio_co_wake
 * because it is meant to be used outside coroutine context; in that case, the
 * coroutine is entered immediately, before qemu_co_enter_next returns.
 *
 * If used in coroutine context, qemu_co_enter_next is equivalent to
 * qemu_co_queue_next.
 */
#define qemu_co_enter_next(queue, lock) \
    qemu_co_enter_next_impl(queue, QEMU_MAKE_LOCKABLE(lock))
bool qemu_co_enter_next_impl(CoQueue *queue, QemuLockable *lock);

/**
 * Checks if the CoQueue is empty.
 */
bool qemu_co_queue_empty(CoQueue *queue);


typedef struct CoRwTicket CoRwTicket;
typedef struct CoRwlock {
    CoMutex mutex;

    /* Number of readers, or -1 if owned for writing.  */
    int owners;

    /* Waiting coroutines.  */
    QSIMPLEQ_HEAD(, CoRwTicket) tickets;
} CoRwlock;

/**
 * Initialises a CoRwlock. This must be called before any other operation
 * is used on the CoRwlock
 */
void qemu_co_rwlock_init(CoRwlock *lock);

/**
 * Read locks the CoRwlock. If the lock cannot be taken immediately because
 * of a parallel writer, control is transferred to the caller of the current
 * coroutine.
 */
CoroutineAction qemu_co_rwlock_rdlock(CoRwlock *lock);

/**
 * Write Locks the CoRwlock from a reader.  This is a bit more efficient than
 * @qemu_co_rwlock_unlock followed by a separate @qemu_co_rwlock_wrlock.
 * Note that if the lock cannot be upgraded immediately, control is transferred
 * to the caller of the current coroutine; another writer might run while
 * @qemu_co_rwlock_upgrade blocks.
 */
CoroutineAction qemu_co_rwlock_upgrade(CoRwlock *lock);

/**
 * Downgrades a write-side critical section to a reader.  Downgrading with
 * @qemu_co_rwlock_downgrade never blocks, unlike @qemu_co_rwlock_unlock
 * followed by @qemu_co_rwlock_rdlock.  This makes it more efficient, but
 * may also sometimes be necessary for correctness.
 */
CoroutineAction qemu_co_rwlock_downgrade(CoRwlock *lock);

/**
 * Write Locks the mutex. If the lock cannot be taken immediately because
 * of a parallel reader, control is transferred to the caller of the current
 * coroutine.
 */
CoroutineAction qemu_co_rwlock_wrlock(CoRwlock *lock);

/**
 * Unlocks the read/write lock and schedules the next coroutine that was
 * waiting for this lock to be run.
 */
CoroutineAction qemu_co_rwlock_unlock(CoRwlock *lock);

typedef struct QemuCoSleep {
    Coroutine *to_wake;
} QemuCoSleep;

/**
 * Yield the coroutine for a given duration. Initializes @w so that,
 * during this yield, it can be passed to qemu_co_sleep_wake() to
 * terminate the sleep.
 */
CoroutineAction qemu_co_sleep_ns_wakeable(QemuCoSleep *w,
                                            QEMUClockType type, int64_t ns);

/**
 * Yield the coroutine until the next call to qemu_co_sleep_wake.
 */
CoroutineAction qemu_co_sleep(QemuCoSleep *w);

static inline CoroutineAction qemu_co_sleep_ns(QEMUClockType type, int64_t ns)
{
    QemuCoSleep w = { 0 };
    return qemu_co_sleep_ns_wakeable(&w, type, ns);
}

/**
 * Wake a coroutine if it is sleeping in qemu_co_sleep_ns. The timer will be
 * deleted. @sleep_state must be the variable whose address was given to
 * qemu_co_sleep_ns() and should be checked to be non-NULL before calling
 * qemu_co_sleep_wake().
 */
void qemu_co_sleep_wake(QemuCoSleep *w);

/**
 * Yield until a file descriptor becomes readable
 *
 * Note that this function clobbers the handlers for the file descriptor.
 */
CoroutineAction yield_until_fd_readable(int fd);

/**
 * Increase coroutine pool size
 */
void qemu_coroutine_increase_pool_batch_size(unsigned int additional_pool_size);

/**
 * Devcrease coroutine pool size
 */
void qemu_coroutine_decrease_pool_batch_size(unsigned int additional_pool_size);

#include "qemu/co-lockable.h"

void *coroutine_only_fn stack_alloc(CoroutineImpl *func, size_t bytes);
CoroutineAction coroutine_only_fn stack_free(CoroutineFrame *f);


#define CO_DO(MACRO, ...) CO_DO_(MACRO, __VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define CO_DO_(MACRO, a0, a1, a2, a3, a4, a5, a6, a7, a8 , a9, n, ...) CO_DO##n(MACRO, a0, a1, a2, a3, a4, a5, a6, a7, a8 , a9)
#define CO_DO0(MACRO, a0, ...)
#define CO_DO1(MACRO, a0, ...) MACRO(a0)
#define CO_DO2(MACRO, a0, ...) MACRO(a0); CO_DO1(MACRO, __VA_ARGS__)
#define CO_DO3(MACRO, a0, ...) MACRO(a0); CO_DO2(MACRO, __VA_ARGS__)
#define CO_DO4(MACRO, a0, ...) MACRO(a0); CO_DO3(MACRO, __VA_ARGS__)
#define CO_DO5(MACRO, a0, ...) MACRO(a0); CO_DO4(MACRO, __VA_ARGS__)
#define CO_DO6(MACRO, a0, ...) MACRO(a0); CO_DO5(MACRO, __VA_ARGS__)
#define CO_DO7(MACRO, a0, ...) MACRO(a0); CO_DO6(MACRO, __VA_ARGS__)
#define CO_DO8(MACRO, a0, ...) MACRO(a0); CO_DO7(MACRO, __VA_ARGS__)
#define CO_DO9(MACRO, a0, ...) MACRO(a0); CO_DO8(MACRO, __VA_ARGS__)

#define CO_FRAME1(decl) decl
#define CO_SAVE1(var) _f->var = var
#define CO_LOAD1(var) var = _f->var
#define CO_DECLARE1(var) typeof(_f->var) var
#define CO_ARG1(var) typeof(_f->var) var = _f->var

#define CO_SAVE(...) CO_DO(CO_SAVE1, __VA_ARGS__)
#define CO_LOAD(...) CO_DO(CO_LOAD1, __VA_ARGS__)
#define CO_DECLARE(...) CO_DO(CO_DECLARE1, __VA_ARGS__)
#define CO_ARG(...) CO_DO(CO_ARG1, __VA_ARGS__)

#define CO_DECLARE_FRAME(func, ...) \
    struct FRAME__##func { \
        CoroutineFrame common; \
        uint32_t _step; \
        CO_DO(CO_FRAME1, __VA_ARGS__); \
    }

#define CO_INIT_FRAME(func, ...) \
    co__##func(({ \
        struct FRAME__##func *_f; \
        _f = stack_alloc(co__##func, sizeof(*_f)); \
        __VA_OPT__(CO_SAVE(__VA_ARGS__);) \
        _f->_step = 0; \
        _f; \
    }))

#endif /* QEMU_COROUTINE_H */
