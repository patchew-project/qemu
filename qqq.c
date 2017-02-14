#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "qqq.h"
/* This is a Linux only feature */

#ifndef _WIN32

#include <arpa/inet.h>
#include <stdint.h>
#include <unistd.h>
#include <assert.h>

static bool enabled = false, syncing = true;
static unsigned elapsed; /* initialized to zero */
static int time_advance = -1;
static int fd = -1;
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
    /* kvm-all.c will call this function before running
     * instructions with kvm. Because syncing will be
     * true while qqq is waiting for a new time advance
     * from the simulation, no instructions will execute
     * while the machine is supposed to be suspended in
     * simulation time.
     */
    qemu_mutex_lock(&qqq_mutex);
    while (syncing) {
        qemu_cond_wait(&qqq_cond, &qqq_mutex);
    }
    qemu_mutex_unlock(&qqq_mutex);
}

static void cleanup_and_exit(void)
{
    /* Close the socket and quit */
    close(fd);
    exit(0);
}

static void start_emulator(void)
{
    if (kvm_enabled()) {
        /* Setting syncing to false tells kvm-all that
         * it can execute guest instructions.
         */
        qemu_mutex_lock(&qqq_mutex);
        syncing = false;
        qemu_mutex_unlock(&qqq_mutex);
        qemu_cond_signal(&qqq_cond);
        /* Restart the emulator clock */
        cpu_enable_ticks();
    }
}

static void stop_emulator(void)
{
    if (kvm_enabled()) {
        /* Tell the emulator that it is not allowed to
         * execute guest instructions.
         */
        qemu_mutex_lock(&qqq_mutex);
        syncing = true;
        qemu_mutex_unlock(&qqq_mutex);
        /* Kick KVM off of the CPU and stop the emulator clock. */
        cpu_disable_ticks();
        kick_all_vcpus();
    }
}

static void write_mem_value(unsigned val)
{
    uint32_t msg = htonl(val);
    if (write(fd, &msg, sizeof(uint32_t)) != sizeof(uint32_t)) {
        /* If the socket is no good, then assume this is an
         * indication that we should exit.
         */
        cleanup_and_exit();
    }
}

static unsigned read_mem_value(void)
{
    uint32_t msg;
    if (read(fd, &msg, sizeof(uint32_t)) != sizeof(uint32_t)) {
        /* If the socket is no good, then assume this is an
         * indication that we should exit.
         */
        cleanup_and_exit();
    }
    return ntohl(msg);
}

static void schedule_next_event(void)
{
    /* If we got the time advance in fd_read, then don't do it
     * again here. */
    if (time_advance < 0) {
        /* Otherwise read the value from the socket */
        time_advance = read_mem_value();
    }
    assert(t == 0 ||
        abs(t - qemu_clock_get_us(QEMU_CLOCK_VIRTUAL)) <= time_advance);
    /* Schedule the next synchronization point */
    timer_mod(sync_timer, t + time_advance);
    /* Note that we need to read the time advance again on the next pass */
    time_advance = -1;
    /* Start advancing cpu ticks and the wall clock */
    start_emulator();
}

static void sync_func(void *data)
{
    /* Stop advancing cpu ticks and the wall clock */
    stop_emulator();
    /* Report the actual elapsed time to the external simulator. */
    int64_t tnow = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    elapsed = tnow - t;
    write_mem_value(elapsed);
    /* Update our time of last event */
    t = tnow;
    /* Schedule the next event */
    schedule_next_event();
}

static void fd_read(void *opaque)
{
    /* Read the time advance if its becomes available
     * before our timer expires */
    time_advance = read_mem_value();
}

void setup_qqq(QemuOpts *opts)
{
    /* The module has been enabled */
    enabled = true;
    if (kvm_enabled()) {
        qemu_mutex_init(&qqq_mutex);
        qemu_cond_init(&qqq_cond);
    }
    /* Stop the clock while the simulation is initialized */
    stop_emulator();
    /* Initialize the simulation clock */
    t = 0;
    /* Get the communication socket */
    fd = qemu_opt_get_number(opts, "sock", 0);
    /* Start the timer to ensure time warps advance the clock */
    sync_timer = timer_new_us(QEMU_CLOCK_VIRTUAL, sync_func, NULL);
    /* Get the time advance that is requested by the simulation */
    schedule_next_event();
    /* Register the file descriptor with qemu. This should ensure
     * the emulator doesn't pause for lack of I/O and thereby
     * cause the attached simulator to pause with it. */
    qemu_set_fd_handler(fd, fd_read, NULL, NULL);
}

#else

void setup_qqq(QemuOpts *opts)
{
    fprintf(stderr, "-qqq is not supported on Windows, exiting\n");
    exit(0);
}

#endif
