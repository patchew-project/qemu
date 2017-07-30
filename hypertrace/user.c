/*
 * QEMU-side management of hypertrace in user-level emulation.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * Implementation details
 * ======================
 *
 * There are 3 channels, each a regular file in the host system, and mmap'ed by
 * the guest application.
 *
 * - Configuration channel: Exposes configuration parameters. Mapped once and
 *   directly readable.
 *
 * - Data channel: Lets guests write argument values. Each guest thread should
 *   use a different offset to avoid concurrency problems. Mapped once and
 *   directly accessible.
 *
 * - Control channel: Triggers the hypertrace event on a write, providing the
 *   first argument. Offset in the control channel sets the offset in the data
 *   channel. Mapped once per thread, using two pages to reliably detect
 *   accesses and their written value through a SEGV handler.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "qemu/osdep.h"
#include "cpu.h"

#include "hypertrace/common.h"
#include "hypertrace/user.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "trace.h"


static struct hypertrace_config config;
static char *config_path;
static int config_fd = -1;
static uint64_t *qemu_config;

static char *data_path;
static int data_fd = -1;
static uint64_t *qemu_data;

static char *control_path;
static int control_fd = -1;
static uint64_t *qemu_control;
static struct stat control_fd_stat;


QemuOptsList qemu_hypertrace_opts = {
    .name = "hypertrace",
    .implied_opt_name = "path",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_hypertrace_opts.head),
    .desc = {
        {
            .name = "path",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "max-clients",
            .type = QEMU_OPT_NUMBER,
            .def_value_str = "1",
        },
        { /* end of list */ }
    },
};

void hypertrace_opt_parse(const char *optarg, char **base,
                          unsigned int *max_clients_)
{
    int max_clients;
    QemuOpts *opts = qemu_opts_parse_noisily(qemu_find_opts("hypertrace"),
                                             optarg, true);
    if (!opts) {
        exit(1);
    }
    if (qemu_opt_get(opts, "path")) {
        *base = g_strdup(qemu_opt_get(opts, "path"));
    } else {
        error_report("error: -hypertrace path is mandatory");
        exit(EXIT_FAILURE);
    }
    max_clients = qemu_opt_get_number(opts, "max-clients", 1);
    if (max_clients <= 0) {
        error_report("error:"
                     " -hypertrace max-clients expects a positive number");
        exit(EXIT_FAILURE);
    }
    *max_clients_ = max_clients;
}

static void init_channel(const char *base, const char *suffix, size_t size,
                         char **path, int *fd, uint64_t **addr)
{
    *path = g_malloc(strlen(base) + strlen(suffix) + 1);
    sprintf(*path, "%s%s", base, suffix);

    *fd = open(*path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (*fd == -1) {
        error_report("error: open(%s): %s", *path, strerror(errno));
        exit(1);
    }

    off_t lres = lseek(*fd, size - 1, SEEK_SET);
    if (lres == (off_t)-1) {
        error_report("error: lseek(%s): %s", *path, strerror(errno));
        abort();
    }

    char tmp;
    ssize_t wres = write(*fd, &tmp, 1);
    if (wres == -1) {
        error_report("error: write(%s): %s", *path, strerror(errno));
        abort();
    }

    if (addr) {
        *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
        if (*addr == MAP_FAILED) {
            error_report("error: mmap(%s): %s", *path, strerror(errno));
            abort();
        }
    }
}

/* Host SIGSEGV cannot be set bu user-mode guests */
static struct sigaction sigsegv_ours;
static struct sigaction sigsegv_next;
static void sigsegv_handler(int signum, siginfo_t *siginfo, void *sigctxt);

static struct sigaction sigint_ours;
static struct sigaction sigint_next;
bool sigint_user_set;
struct sigaction sigint_user;

static struct sigaction sigabrt_ours;
static struct sigaction sigabrt_next;
bool sigabrt_user_set;
struct sigaction sigabrt_user;

typedef void (*sigaction_t)(int, siginfo_t *, void *);

static void reflect_handler(struct sigaction *ours, struct sigaction *next,
                            int signum, siginfo_t *siginfo, void *sigctxt)
{
    if (next->sa_flags & SA_SIGINFO) {
        if (next->sa_sigaction != NULL) {
            next->sa_sigaction(signum, siginfo, sigctxt);
        } else if (next->sa_sigaction == (sigaction_t)SIG_IGN) {
            /* ignore */
        } else if (next->sa_sigaction == (sigaction_t)SIG_DFL) {
            if (signal(signum, SIG_DFL) == SIG_ERR) {
                error_report("error: signal: %s", strerror(errno));
                abort();
            }
            if (raise(signum) != 0) {
                error_report("error: raise: %s", strerror(errno));
                abort();
            }
            struct sigaction tmp;
            memcpy(&tmp, ours, sizeof(tmp));
            if (sigaction(signum, &tmp, NULL) != 0) {
                error_report("error: sigaction: %s", strerror(errno));
                abort();
            }
        }
    } else {
        if (next->sa_handler != NULL) {
            next->sa_handler(signum);
        } else if (next->sa_handler == SIG_IGN) {
            /* ignore */
        } else if (next->sa_handler == SIG_DFL) {
            if (signal(signum, SIG_DFL) == SIG_ERR) {
                error_report("error: signal: %s", strerror(errno));
                abort();
            }
            if (raise(signum) != 0) {
                error_report("error: raise: %s", strerror(errno));
                abort();
            }
            struct sigaction tmp;
            memcpy(&tmp, ours, sizeof(tmp));
            if (sigaction(signum, &tmp, NULL) != 0) {
                error_report("error: sigaction: %s", strerror(errno));
                abort();
            }
        }
    }
}

static void sigint_handler(int signum, siginfo_t *siginfo, void *sigctxt)
{
    hypertrace_fini();
    /* QEMU lets users override any signal handler */
    if (sigint_user_set) {
        reflect_handler(&sigint_ours, &sigint_user, signum, siginfo, sigctxt);
    } else {
        reflect_handler(&sigint_ours, &sigint_next, signum, siginfo, sigctxt);
    }
}

static void sigabrt_handler(int signum, siginfo_t *siginfo, void *sigctxt)
{
    hypertrace_fini();
    /* QEMU lets users override any signal handler */
    if (sigabrt_user_set) {
        reflect_handler(&sigabrt_ours, &sigabrt_user, signum, siginfo, sigctxt);
    } else {
        reflect_handler(&sigabrt_ours, &sigabrt_next, signum, siginfo, sigctxt);
    }
}

void hypertrace_init(const char *base, unsigned int max_clients)
{
    struct hypertrace_config *pconfig;

    if (base == NULL) {
        return;
    }

    sigint_user_set = false;
    memset(&sigint_ours, 0, sizeof(sigint_ours));
    sigint_ours.sa_sigaction = sigint_handler;
    sigint_ours.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sigint_ours.sa_mask);
    if (sigaction(SIGINT, &sigint_ours, &sigint_next) != 0) {
        error_report("error: sigaction(SIGINT): %s", strerror(errno));
        abort();
    }

    sigabrt_user_set = false;
    memset(&sigabrt_ours, 0, sizeof(sigabrt_ours));
    sigabrt_ours.sa_sigaction = sigabrt_handler;
    sigabrt_ours.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sigabrt_ours.sa_mask);
    if (sigaction(SIGABRT, &sigabrt_ours, &sigabrt_next) != 0) {
        error_report("error: sigaction(SIGABRT): %s", strerror(errno));
        abort();
    }

    hypertrace_init_config(&config, max_clients);

    init_channel(base, "-config", getpagesize(),
                 &config_path, &config_fd, &qemu_config);
    pconfig = (struct hypertrace_config *)qemu_config;
    pconfig->max_clients = tswap64(config.max_clients);
    pconfig->client_args = tswap64(config.client_args);
    pconfig->client_data_size = tswap64(config.client_data_size);
    pconfig->control_size = tswap64(config.control_size);
    pconfig->data_size = tswap64(config.data_size);

    init_channel(base, "-data", config.data_size,
                 &data_path, &data_fd, &qemu_data);
    if (fstat(data_fd, &control_fd_stat) == -1) {
        error_report("error: fstat(hypertrace_control): %s", strerror(errno));
        abort();
    }

    init_channel(base, "-control", config.control_size,
                 &control_path, &control_fd, &qemu_control);

    if (fstat(control_fd, &control_fd_stat) == -1) {
        error_report("error: fstat(hypertrace_control): %s", strerror(errno));
        abort();
    }

    memset(&sigsegv_ours, 0, sizeof(sigsegv_ours));
    sigsegv_ours.sa_sigaction = sigsegv_handler;
    sigsegv_ours.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sigsegv_ours.sa_mask);
    if (sigaction(SIGSEGV, &sigsegv_ours, &sigsegv_next) != 0) {
        error_report("error: sigaction(SIGSEGV): %s", strerror(errno));
        abort();
    }
}


static void fini_channel(int *fd, char **path)
{
    if (*fd != -1) {
        if (close(*fd) == -1) {
            error_report("error: close: %s", strerror(errno));
            abort();
        }
        if (unlink(*path) == -1) {
            error_report("error: unlink(%s): %s", *path, strerror(errno));
            abort();
        }
        *fd = -1;
    }
    if (*path != NULL) {
        g_free(*path);
        *path =  NULL;
    }
}

void hypertrace_fini(void)
{
    static bool atexit_in;
    if (atexit_in) {
        return;
    }
    atexit_in = true;

    if (sigaction(SIGSEGV, &sigsegv_next, NULL) != 0) {
        error_report("error: sigaction(SIGSEGV): %s", strerror(errno));
        abort();
    }
    fini_channel(&config_fd, &config_path);
    fini_channel(&data_fd, &data_path);
    fini_channel(&control_fd, &control_path);
}


bool hypertrace_guest_mmap_check(int fd, unsigned long len,
                                 unsigned long offset)
{
    struct stat s;
    if (fstat(fd, &s) < 0) {
        /* the control channel should never fail fstat() */
        return true;
    }

    if (s.st_dev != control_fd_stat.st_dev ||
        s.st_ino != control_fd_stat.st_ino) {
        /* this is not the control channel */
        return true;
    }

    /* check control channel is mapped correctly */
    return len == (config.control_size) && offset == 0;
}

void hypertrace_guest_mmap_apply(int fd, void *qemu_addr, CPUState *vcpu)
{
    struct stat s;

    if (vcpu == NULL) {
        return;
    }

    if (fstat(fd, &s) != 0) {
        return;
    }

    if (s.st_dev != control_fd_stat.st_dev ||
        s.st_ino != control_fd_stat.st_ino) {
        return;
    }

    /* it's an mmap of the control channel; split it in two and mprotect it to
     * detect writes (cmd is written once on each part)
     */
    vcpu->hypertrace_control = qemu_addr;
    if (mprotect(qemu_addr, config.control_size / 2, PROT_READ) == -1) {
        error_report("error: mprotect(hypertrace_control): %s",
                     strerror(errno));
        abort();
    }
}

static void swap_control(void *from, void *to)
{
    if (mprotect(from, config.control_size / 2, PROT_READ | PROT_WRITE) == -1) {
        error_report("error: mprotect(from): %s", strerror(errno));
        abort();
    }
    if (mprotect(to, config.control_size / 2, PROT_READ) == -1) {
        error_report("error: mprotect(to): %s", strerror(errno));
        abort();
    }
}

static void sigsegv_handler(int signum, siginfo_t *siginfo, void *sigctxt)
{
    CPUState *vcpu = current_cpu;
    void *control_0 = vcpu->hypertrace_control;
    void *control_1 = vcpu->hypertrace_control + config.control_size / 2;
    void *control_2 = control_1 + config.control_size / 2;

    if (control_0 <= siginfo->si_addr && siginfo->si_addr < control_1) {

        /* 1st fault (guest will write cmd) */
        assert(((uintptr_t)siginfo->si_addr % sizeof(TARGET_PAGE_SIZE)) == 0);
        swap_control(control_0, control_1);

    } else if (control_1 <= siginfo->si_addr && siginfo->si_addr < control_2) {

        /* 2nd fault (invoke) */
        size_t client = (siginfo->si_addr - control_1) / sizeof(uint64_t);
        uint64_t vcontrol = ((uint64_t *)control_0)[client];
        uint64_t *data_ptr = &qemu_data[client * config.client_data_size];
        assert(((uintptr_t)siginfo->si_addr % sizeof(uint64_t)) == 0);
        hypertrace_emit(current_cpu, vcontrol, data_ptr);
        swap_control(control_1, control_0);

    } else {

        /* proxy to next handler */
        reflect_handler(&sigsegv_ours, &sigsegv_next, signum, siginfo, sigctxt);

    }
}
