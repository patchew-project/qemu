/* The ThrottleGroup structure (with its ThrottleState) is shared
 * among different BlockDriverStates and it's independent from
 * AioContext, so in order to use it from different threads it needs
 * its own locking.
 *
 * This locking is however handled internally in block/throttle.c so it's
 * transparent to outside users.
 *
 * The whole ThrottleGroup structure is private and invisible to
 * outside users, that only use it through its ThrottleState.
 *
 * In addition to the ThrottleGroup structure, BlockDriverState has
 * fields that need to be accessed by other members of the group and
 * therefore also need to be protected by this lock. Once a
 * BlockDriverState is registered in a group those fields can be accessed
 * by other threads any time.
 *
 * Again, all this is handled internally in block/throttle.c and is mostly
 * transparent to the outside. The 'throttle_timers' field however has an
 * additional constraint because it may be temporarily invalid (see for example
 * bdrv_set_aio_context()). Therefore block/throttle.c will access some
 * other BlockDriverState's timers only after verifying that that BDS has
 * throttled requests in the queue.
 */

typedef struct ThrottleGroup {
    char *name; /* This is constant during the lifetime of the group */

    QemuMutex lock; /* This lock protects the following four fields */
    ThrottleState ts;
    QLIST_HEAD(, BDRVThrottleNodeState) head;
    struct BDRVThrottleNodeState *tokens[2];
    bool any_timer_armed[2];

    /* These two are protected by the global throttle_groups_lock */
    unsigned refcount;
    QTAILQ_ENTRY(ThrottleGroup) list;
} ThrottleGroup;

typedef struct BDRVThrottleNodeState {
    ThrottleGroup *throttle_group;

    /* I/O throttling has its own locking, but also some fields are
     * protected by the AioContext lock.
     */

    /* Protected by AioContext lock.  */
    CoQueue      throttled_reqs[2];

    /* Nonzero if the I/O limits are currently being ignored; generally
     * it is zero.  */
    unsigned int io_limits_disabled;

    ThrottleState *throttle_state;
    ThrottleTimers throttle_timers;
    unsigned       pending_reqs[2];
    QLIST_ENTRY(BDRVThrottleNodeState) round_robin;

} BDRVThrottleNodeState;
