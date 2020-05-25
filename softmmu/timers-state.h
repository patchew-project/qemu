#ifndef TIMERS_STATE_H
#define TIMERS_STATE_H

/* timers state, for sharing between icount and cpu-timers */

typedef struct TimersState {
    /* Protected by BQL.  */
    int64_t cpu_ticks_prev;
    int64_t cpu_ticks_offset;

    /*
     * Protect fields that can be respectively read outside the
     * BQL, and written from multiple threads.
     */
    QemuSeqLock vm_clock_seqlock;
    QemuSpin vm_clock_lock;

    int16_t cpu_ticks_enabled;

    /* Conversion factor from emulated instructions to virtual clock ticks.  */
    int16_t icount_time_shift;

    /* Compensate for varying guest execution speed.  */
    int64_t qemu_icount_bias;

    int64_t vm_clock_warp_start;
    int64_t cpu_clock_offset;

    /* Only written by TCG thread */
    int64_t qemu_icount;

    /* for adjusting icount */
    QEMUTimer *icount_rt_timer;
    QEMUTimer *icount_vm_timer;
    QEMUTimer *icount_warp_timer;
} TimersState;

extern TimersState timers_state;

/*
 * icount needs this internal from cpu-timers when adjusting the icount shift.
 */
int64_t cpu_get_clock_locked(void);

#endif /* TIMERS_STATE_H */
