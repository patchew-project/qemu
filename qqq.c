#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "qqq.h"

/* This is a Linux only feature */

#ifndef _WIN32

#include <unistd.h>
#include <assert.h>

static bool enabled = false, syncing = false;
static int elapsed; // This must be zero on initialization
static int time_advance = -1;
static int read_fd = -1, write_fd = -1;
static int64_t t;
static QEMUTimer *sync_timer;
static QemuMutex qqq_mutex;
static QemuCond qqq_cond;

bool qqq_enabled(void)
{
    return enabled;
}

void qqq_sync(void)
{
    qemu_mutex_lock(&qqq_mutex);
    while (syncing) {
        qemu_cond_wait(&qqq_cond, &qqq_mutex);
    }
    qemu_mutex_unlock(&qqq_mutex);
}

static void cleanup_and_exit(void)
{
    close(read_fd);
    close(write_fd);
    exit(0);
}

static void write_mem_value(int val)
{
    if (write(write_fd, &val, sizeof(int)) != sizeof(int)) {
        /* If the pipe is no good, then assume this is an
         * indication that we should exit.
         */
        cleanup_and_exit();
    }
}

static int read_mem_value(void)
{
    int tmp;
    if (read(read_fd, &tmp, sizeof(int)) != sizeof(int)) {
        /* If the pipe is no good, then assume this is an
         * indication that we should exit.
         */
        cleanup_and_exit();
    }
    return tmp;
}

static void schedule_next_event(bool is_init)
{
    /* If we got the time advance in fd_read, then don't do it
     * again here. */
    if (time_advance < 0) 
        /* Otherwise read the value from the pipe */
        time_advance = read_mem_value();
    /* Release kvm to go forward in time to the next synchronization point */
    if (!is_init && kvm_enabled()) {
        qemu_mutex_lock(&qqq_mutex);
        syncing = false;
        qemu_mutex_unlock(&qqq_mutex);
        qemu_cond_signal(&qqq_cond);
        thaw_time();
    }
    /* Schedule the next synchronization point */
    timer_mod(sync_timer, t + time_advance);
    /* Note that we need to read the time advance again on the next pass */
    time_advance = -1;
}

static void sync_func(void *data)
{
    if (kvm_enabled()) {
        /* Set the sync flag that will cause the vcpu to wait for synchronization
         * to finish before it begins executing instructions again */
        qemu_mutex_lock(&qqq_mutex);
        syncing = true;
        qemu_mutex_unlock(&qqq_mutex);
        /* Kick KVM off of the processor and keep it off while we synchronize */
        kick_all_vcpus();
        /* Stop advancing cpu ticks and the wall clock */
        freeze_time();
    }
    /* Report the actual elapsed time to the external simulator. */
    int64_t tnow = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    elapsed = tnow - t;
    write_mem_value(elapsed);
    /* Update our time of last event */
    t = tnow;
    /* Schedule the next event */
    schedule_next_event(false);
}

static void fd_read(void *opaque)
{
    /* Read the time advance if its becomes available
     * before our timer expires */
       time_advance = read_mem_value();
}

void setup_qqq(QemuOpts *opts)
{
    /* Stop the clock while the simulation is initialized */
    if (kvm_enabled()) {
        qemu_mutex_init(&qqq_mutex);
        qemu_cond_init(&qqq_cond);
    }
    /* Initialize the simulation clock */
    t = 0;
    /* Get the communication pipes */
    read_fd = qemu_opt_get_number(opts, "read", 0);
    write_fd = qemu_opt_get_number(opts, "write", 0);
    /* Start the timer to ensure time warps advance the clock */
    sync_timer = timer_new_us(QEMU_CLOCK_VIRTUAL, sync_func, NULL);
    /* Get the time advance that is requested by the simulation */
    schedule_next_event(true);
    /* Register the file descriptor with qemu. This should ensure
     * the emulator doesn't pause for lack of I/O and thereby
     * cause the attached simulator to pause with it. */
    qemu_set_fd_handler(read_fd, fd_read, NULL, NULL);
    /* The module has been enabled */
    enabled = true;
}

#else

void setup_qqq(QemuOpts *opts)
{
    fprintf(stderr, "-qqq is not supported on Windows, exiting\n");
    exit(0);
}

#endif
