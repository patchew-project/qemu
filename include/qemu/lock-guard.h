#ifndef QEMU_LOCK_GUARD_H
#define QEMU_LOCK_GUARD_H 1

typedef void QemuLockGuardFunc(void *);
typedef struct QemuLockGuard {
    QemuLockGuardFunc *p_lock_fn, *p_unlock_fn;
    void *lock;
    int locked;
} QemuLockGuard;

static inline void qemu_lock_guard_lock(QemuLockGuard *lock_guard)
{
    assert(!lock_guard->locked);
    lock_guard->p_lock_fn(lock_guard->lock);
    lock_guard->locked = true;
}

static inline void qemu_lock_guard_unlock(QemuLockGuard *lock_guard)
{
    assert(lock_guard->locked);
    lock_guard->locked = false;
    lock_guard->p_unlock_fn(lock_guard->lock);
}

static inline bool qemu_lock_guard_is_taken(QemuLockGuard *lock_guard)
{
    return lock_guard->locked;
}

static inline void qemu_lock_guard_release(QemuLockGuard *lock_guard)
{
    lock_guard->lock = NULL;
    lock_guard->locked = false;
}

inline void qemu_lock_guard_pass(void *ptr)
{
    QemuLockGuard *lock_guard = ptr;
    assert(lock_guard->locked || !lock_guard->lock);
}

inline void qemu_lock_guard_cleanup(void *ptr)
{
    QemuLockGuard *lock_guard = ptr;
    if (likely(lock_guard->locked)) {
        lock_guard->p_unlock_fn(lock_guard->lock);
    }
}

static inline QemuLockGuard qemu_lock_guard_init(QemuLockGuard lock_guard)
{
    qemu_lock_guard_lock(&lock_guard);
    return lock_guard;
}

#define QEMU_INIT_LOCK_GUARD(type, lock_fn, unlock_fn)                     \
    .p_lock_fn   = (QemuLockGuardFunc *) (void (*) (type *)) lock_fn,      \
    .p_unlock_fn = (QemuLockGuardFunc *) (void (*) (type *)) unlock_fn

#define QEMU_LOCK_GUARD_(type, lock, locked)                               \
    (QemuLockGuard) {                                                      \
        QEMU_LOCK_GUARD_FUNCS_##type,                                      \
        lock + type_check(typeof(*lock), type),                            \
        locked                                                             \
    }

/* Take a lock that will be unlocked on returning */
#define QEMU_LOCK_GUARD(type, name, lock)                                  \
    QemuLockGuard __attribute__((cleanup(qemu_lock_guard_cleanup))) name = \
        qemu_lock_guard_init(QEMU_LOCK_GUARD_(type, lock, false))

#define QEMU_WITH_LOCK(type, name, lock)                                   \
    for (QEMU_LOCK_GUARD(type, name, lock);                                \
         qemu_lock_guard_is_taken(&name);                                  \
         qemu_lock_guard_unlock(&name))

/* Create a QemuLockGuard for a lock that is taken and will be unlocked on
 * returning
 */
#define QEMU_ADOPT_LOCK(type, name, lock)                                  \
    QemuLockGuard __attribute__((cleanup(qemu_lock_guard_cleanup))) name = \
        QEMU_LOCK_GUARD_(type, lock, true)

#define QEMU_WITH_ADOPTED_LOCK(type, name, lock)                           \
    for (QEMU_ADOPT_LOCK(type, name, lock);                                \
         qemu_lock_guard_is_taken(&name);                                  \
         qemu_lock_guard_unlock(&name))

/* Take a lock and create a QemuLockGuard for it, asserting that it will
 * be locked when returning.
 */
#define QEMU_RETURN_LOCK(type, name, lock)                                 \
    QemuLockGuard __attribute__((cleanup(qemu_lock_guard_pass))) name =    \
        qemu_lock_guard_init(QEMU_LOCK_GUARD_(type, lock, false))

/* Create a QemuLockGuard for a lock that is taken and must be locked
 * when returning
 */
#define QEMU_TAKEN_LOCK(type, name, lock)                                  \
    QemuLockGuard __attribute__((cleanup(qemu_lock_guard_pass))) name =    \
        QEMU_LOCK_GUARD_(type, lock, true)

#endif
