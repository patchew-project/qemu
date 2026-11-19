/*
 * macOS poll/sleep wait-primitive benchmark.
 *
 * Models qemu_poll_ns() on a host without ppoll(2): wait on an idle pipe
 * read end for one period, repeatedly, with the timer re-armed at fire
 * time. The poll, pselect and kevent variants watch the pipe; nanosleep,
 * the one-shot timer and the Mach variants only sleep. Reports the
 * interval distribution (p10/median/p90/max), the CPU time per wait
 * (getrusage) and the wakeup rate.
 *
 * Variants: poll with the timeout rounded up to ms (what g_poll() does),
 * poll truncated to ms, pselect/kevent/nanosleep with ns resolution, a
 * one-shot EVFILT_TIMER kevent, the poll+nanosleep hybrid used by the
 * Darwin branch of qemu_poll_ns(), plus --all extras (poll+spin,
 * mach_wait_until() kicker thread, THREAD_TIME_CONSTRAINT_POLICY).
 *
 * Usage: macos-pollbench [period_ns] [iters] [--all]
 * Default: 1047000 ns (1.047 ms, one serial character at 9600 8N1), 3000
 * iterations.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <sys/event.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/time.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int cmp(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return x < y ? -1 : x > y;
}

static int pfd[2], kq;

/* poll with timeout rounded up to whole ms (g_poll / qemu_timeout_ns_to_ms) */
static void w_poll_ms_up(int64_t ns)
{
    struct pollfd p = { pfd[0], POLLIN, 0 };
    poll(&p, 1, (int)((ns + 999999) / 1000000));
}

/* poll with timeout truncated to whole ms: busy-waits below 1 ms */
static void w_poll_ms_down(int64_t ns)
{
    struct pollfd p = { pfd[0], POLLIN, 0 };
    poll(&p, 1, (int)(ns / 1000000));
}

static void w_pselect(int64_t ns)
{
    fd_set r;
    FD_ZERO(&r);
    FD_SET(pfd[0], &r);
    struct timespec ts = { ns / 1000000000LL, ns % 1000000000LL };
    pselect(pfd[0] + 1, &r, NULL, NULL, &ts, NULL);
}

static void w_kevent(int64_t ns)
{
    struct kevent ev;
    struct timespec ts = { ns / 1000000000LL, ns % 1000000000LL };
    kevent(kq, NULL, 0, &ev, 1, &ts);
}

static void w_nanosleep(int64_t ns)
{
    struct timespec ts = { ns / 1000000000LL, ns % 1000000000LL };
    nanosleep(&ts, NULL);
}

/*
 * poll whole ms (floor), then nanosleep the sub-ms remainder (Darwin
 * qemu_poll_ns())
 */
static void w_hybrid(int64_t ns)
{
    int64_t start = now_ns();
    struct pollfd p = { pfd[0], POLLIN, 0 };
    int r = poll(&p, 1, (int)(ns / 1000000));
    if (r == 0) {
        int64_t rem = ns - (now_ns() - start);
        if (rem > 0) {
            struct timespec ts = { rem / 1000000000LL, rem % 1000000000LL };
            nanosleep(&ts, NULL);
        }
    }
}

/* poll whole ms, then spin the sub-ms remainder */
static void w_poll_spin(int64_t ns)
{
    int64_t end = now_ns() + ns;
    struct pollfd p = { pfd[0], POLLIN, 0 };
    if (ns >= 1000000) {
        poll(&p, 1, (int)(ns / 1000000) - 1);
    }
    while (now_ns() < end) {
        /* spin */
    }
}

/* one-shot EVFILT_TIMER with ns resolution, re-armed each call */
static void w_kevent_timer(int64_t ns)
{
    struct kevent ev, out;
    EV_SET(&ev, 1 /*ident*/, EVFILT_TIMER, EV_ADD | EV_ONESHOT,
           NOTE_NSECONDS, ns, NULL);
    kevent(kq, &ev, 1, &out, 1, NULL);
}

/*
 * kicker thread with mach_wait_until(); the main thread blocks on a Mach
 * semaphore. Deadline computed in the helper each round.
 */
static semaphore_t sem;
static mach_timebase_info_data_t tbi;
static int64_t kick_period;
/* written by main, polled by the kicker; no locking needed in a benchmark */
static volatile int kick_run = 1;
static pthread_t kick_thread;

static void *kicker(void *arg)
{
    (void)arg;
    while (kick_run) {
        uint64_t dl = mach_absolute_time() +
            (uint64_t)((double)kick_period * tbi.denom / tbi.numer);
        mach_wait_until(dl);
        semaphore_signal(sem);
    }
    return NULL;
}

static int mach_thread_setup(int64_t ns)
{
    kick_period = ns; /* set before the thread starts: kick_period=0 spins */
    kick_run = 1;
    return pthread_create(&kick_thread, NULL, kicker, NULL);
}

/* stop the kicker so it cannot perturb the following variants */
static void mach_thread_teardown(void)
{
    kick_run = 0;
    pthread_join(kick_thread, NULL);
}

static void w_mach_thread(int64_t ns)
{
    semaphore_wait(sem); /* blocking; the helper always signals */
}

/* run under THREAD_TIME_CONSTRAINT_POLICY */
static int set_time_constraint(int64_t ns)
{
    thread_time_constraint_policy_data_t pol;
    thread_port_t self = pthread_mach_thread_np(pthread_self());
    uint64_t per = (uint64_t)((double)ns * tbi.denom / tbi.numer);

    pol.period = per;
    pol.computation = per / 2;
    pol.constraint = per;
    pol.preemptible = 0;
    return thread_policy_set(self, THREAD_TIME_CONSTRAINT_POLICY,
                             (thread_policy_t)&pol,
                             THREAD_TIME_CONSTRAINT_POLICY_COUNT);
}

struct variant {
    const char *name;
    void (*wait)(int64_t);
    int (*setup)(int64_t);      /* returns 0 on success, else variant skipped */
    void (*teardown)(void);
    bool extra;                 /* only run with --all */
};

static void run(struct variant *v, int64_t period, int n)
{
    struct rusage r0, r1;
    int64_t *iv = malloc(n * sizeof(*iv));
    int64_t last;
    int64_t total = 0;
    double cpu;

    if (!iv) {
        perror("malloc");
        exit(1);
    }

    if (v->setup && v->setup(period)) {
        printf("%-14s skipped: setup failed\n", v->name);
        free(iv);
        return;
    }
    getrusage(RUSAGE_SELF, &r0);
    last = now_ns();
    for (int i = 0; i < n; i++) {
        v->wait(period);
        int64_t t = now_ns();
        iv[i] = t - last;
        last = t;
    }
    getrusage(RUSAGE_SELF, &r1);
    if (v->teardown) {
        v->teardown();
    }
    cpu = (r1.ru_utime.tv_sec - r0.ru_utime.tv_sec) +
          (r1.ru_stime.tv_sec - r0.ru_stime.tv_sec) +
          (r1.ru_utime.tv_usec - r0.ru_utime.tv_usec) * 1e-6 +
          (r1.ru_stime.tv_usec - r0.ru_stime.tv_usec) * 1e-6;
    for (int i = 0; i < n; i++) {
        total += iv[i];
    }
    qsort(iv, n, sizeof(*iv), cmp);
    printf("%-14s p10 %.3f  median %.3f  p90 %.3f  max %.3f ms"
           "  cpu %6.1f us/wait  wakes/s %8.1f\n",
           v->name, iv[n / 10] / 1e6, iv[n / 2] / 1e6, iv[n * 9 / 10] / 1e6,
           iv[n - 1] / 1e6, cpu / n * 1e6, n / (total / 1e9));
    free(iv);
}

int main(int argc, char **argv)
{
    int64_t period = argc > 1 ? atoll(argv[1]) : 1047000;
    int n = argc > 2 ? atoi(argv[2]) : 3000;
    int all = argc > 3 && !strcmp(argv[3], "--all");
    struct kevent kev;

    if (argc > 4 || (argc > 3 && !all) || period <= 0 || n <= 0) {
        fprintf(stderr, "usage: %s [period_ns>0] [iters>0] [--all]\n", argv[0]);
        return 1;
    }
    /* tcon poll changes the thread policy for good: keep it last */
    struct variant vs[] = {
        { "poll ms-up",   w_poll_ms_up },
        { "poll ms-down", w_poll_ms_down },
        { "pselect ns",   w_pselect },
        { "kevent ns",    w_kevent },
        { "kevent-timer", w_kevent_timer },
        { "nanosleep",    w_nanosleep },
        { "hybrid",       w_hybrid },
        { "poll+spin",    w_poll_spin, NULL, NULL, true },
        { "mach-thread",  w_mach_thread, mach_thread_setup,
          mach_thread_teardown, true },
        { "tcon poll",    w_poll_ms_down, set_time_constraint, NULL, true },
    };

    if (pipe(pfd) < 0) {
        return 1;
    }
    kq = kqueue();
    if (kq < 0) {
        perror("kqueue");
        return 1;
    }
    EV_SET(&kev, pfd[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0) {
        perror("kevent register");
        return 1;
    }
    mach_timebase_info(&tbi);
    semaphore_create(mach_task_self(), &sem, SYNC_POLICY_FIFO, 0);
    printf("period %.3f ms, %d iterations\n", period / 1e6, n);
    for (size_t i = 0; i < sizeof(vs) / sizeof(vs[0]); i++) {
        if (all || !vs[i].extra) {
            run(&vs[i], period, n);
        }
    }
    return 0;
}
