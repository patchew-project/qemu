/*
 * linux-user "badframe" signal handling tests.
 *
 * Copyright (c) 2026 Tenstorrent USA, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Test "badframe" signal handling paths, which force a
 * SIGSEGV signal from the signal handler setup code,
 * which tests the recursive signal "restart_scan" logic
 * in process_pending_signals().
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <sys/time.h>
#include <sys/mman.h>

#undef DEBUG
#ifdef DEBUG
#define dprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...)
#endif

static void error1(const char *filename, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d: ", filename, line);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static int __chk_error(const char *filename, int line, int ret)
{
    if (ret < 0) {
        error1(filename, line, "%m (ret=%d, errno=%d/%s)",
               ret, errno, strerror(errno));
    }
    return ret;
}

#define error(fmt, ...) error1(__FILE__, __LINE__, fmt, ## __VA_ARGS__)

#define chk_error(ret) __chk_error(__FILE__, __LINE__, (ret))

static bool do_siglongjmp;
static sigjmp_buf current_sigjmp_buf;

static volatile int total_alarm_count;

static void sig_alarm(int sig, siginfo_t *info, void *puc)
{
    if (sig != SIGRTMIN) {
        error("unexpected signal");
    }
    dprintf("SIGRTMIN\n");
    total_alarm_count++;
}

static volatile int total_segv_count;

static void sig_segv(int sig, siginfo_t *info, void *puc)
{
    if (sig != SIGSEGV) {
        error("unexpected signal");
    }
    dprintf("SIGSEGV\n");
    total_segv_count++;
    if (do_siglongjmp) {
        dprintf("siglongjmp()\n");
        siglongjmp(current_sigjmp_buf, 1);
    }
}

static volatile int total_trap_count;

static void sig_trap(int sig, siginfo_t *info, void *puc)
{
    if (sig == SIGTRAP) {
        dprintf("SIGTRAP\n");
    } else if (sig == SIGILL) {
        dprintf("SIGILL\n");
    } else if (sig == SIGABRT) {
        dprintf("SIGABRT\n");
    } else {
        error("unexpected signal");
    }
    total_trap_count++;
    if (do_siglongjmp) {
        dprintf("siglongjmp()\n");
        siglongjmp(current_sigjmp_buf, 1);
    }
}

static void test_signals(void)
{
    struct sigaction act;
    struct itimerspec it;
    timer_t tid;
    struct sigevent sev;
    stack_t ss;
    void *mem;

    /* Set up SEGV handler */
    act.sa_sigaction = sig_segv;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    chk_error(sigaction(SIGSEGV, &act, NULL));

    /* Set up an altstack */
    mem = mmap(NULL, SIGSTKSZ, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
            fprintf(stderr, "out of memory");
            exit(EXIT_FAILURE);
    }

    ss.ss_sp = mem;
    ss.ss_flags = 0;
    ss.ss_size = SIGSTKSZ;
    chk_error(sigaltstack(&ss, NULL));

    /* Async signal test */

    /* Set up RTMIN handler on alt stack */
    act.sa_sigaction = sig_alarm;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO | SA_ONSTACK;
    chk_error(sigaction(SIGRTMIN, &act, NULL));

    /* Create POSIX timer */
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = &tid;
    chk_error(timer_create(CLOCK_REALTIME, &sev, &tid));

    it.it_interval.tv_sec = 0;
    it.it_interval.tv_nsec = 1000000;
    it.it_value.tv_sec = 0;
    it.it_value.tv_nsec = 1000000;
    chk_error(timer_settime(tid, 0, &it, NULL));

    while (total_alarm_count == 0) {
        usleep(1000);
    }
    total_alarm_count = 0;

    chk_error(timer_delete(tid));

    assert(total_segv_count == 0);

    /* Make the alt stack bad */
    chk_error(mprotect(mem, SIGSTKSZ, PROT_NONE));

    chk_error(timer_create(CLOCK_REALTIME, &sev, &tid));
    chk_error(timer_settime(tid, 0, &it, NULL));

    while (total_segv_count == 0) {
        usleep(1000);
    }
    total_segv_count = 0;

    chk_error(timer_delete(tid));

    assert(total_alarm_count == 0);

    /* Make the alt stack good */
    chk_error(mprotect(mem, SIGSTKSZ, PROT_READ | PROT_WRITE));

    /* Bad sync signal test */

    /* Set up SIGILL/TRAP/ABRT handler on alt stack */
    act.sa_sigaction = sig_trap;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO | SA_ONSTACK;
    chk_error(sigaction(SIGTRAP, &act, NULL));
    chk_error(sigaction(SIGILL, &act, NULL));
    chk_error(sigaction(SIGABRT, &act, NULL));

    if (sigsetjmp(current_sigjmp_buf, 1) == 0) {
        do_siglongjmp = true;
        /* Cause a synchronous signal */
        dprintf("__builtin_trap()\n");
        __builtin_trap();
        assert(0);
    }
    do_siglongjmp = false;
    assert(total_trap_count == 1);
    total_trap_count = 0;
    assert(total_segv_count == 0);

    /* Make the alt stack bad */
    chk_error(mprotect(mem, SIGSTKSZ, PROT_NONE));

    if (sigsetjmp(current_sigjmp_buf, 1) == 0) {
        do_siglongjmp = true;
        /* Cause a synchronous signal */
        dprintf("__builtin_trap()\n");
        __builtin_trap();
        assert(0);
    }
    do_siglongjmp = false;
    assert(total_segv_count == 1);
    total_segv_count = 0;
    assert(total_trap_count == 0);
}

int main(int argc, char **argv)
{
    test_signals();
    return 0;
}
