#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "external_sim.h"

/* This is a Linux only feature */

#ifndef _WIN32

static bool enabled = false, syncing = true;
static unsigned elapsed; /* initialized to zero */
static int time_advance = -1;
static int fd = -1;
static int64_t t;
static QEMUTimer *sync_timer;
static QemuMutex external_sim_mutex;
static QemuCond external_sim_cond;

bool external_sim_enabled(void)
{
    return enabled;
}

void external_sim_sync(void)
{
    /* kvm-all.c will call this function before running
     * instructions with kvm. Because syncing will be
     * true while external_sim is waiting for a new time advance
     * from the simulation, no instructions will execute
     * while the machine is supposed to be suspended in
     * simulation time.
     */
    qemu_mutex_lock(&external_sim_mutex);
    while (syncing) {
        qemu_cond_wait(&external_sim_cond, &external_sim_mutex);
    }
    qemu_mutex_unlock(&external_sim_mutex);
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
        qemu_mutex_lock(&external_sim_mutex);
        syncing = false;
        qemu_mutex_unlock(&external_sim_mutex);
        qemu_cond_signal(&external_sim_cond);
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
        qemu_mutex_lock(&external_sim_mutex);
        syncing = true;
        qemu_mutex_unlock(&external_sim_mutex);
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
    /* Read time advance from the socket */
    time_advance = read_mem_value();
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

void setup_external_sim(QemuOpts *opts)
{
    /* The module has been enabled */
    enabled = true;
    if (kvm_enabled()) {
        qemu_mutex_init(&external_sim_mutex);
        qemu_cond_init(&external_sim_cond);
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
}

#else

void setup_external_sim(QemuOpts *opts)
{
    fprintf(stderr, "-external_sim is not supported on Windows, exiting\n");
    exit(0);
}

#endif
