/*
 * QEMU-side management of hypertrace in user-level emulation.
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * Implementation details
 * ======================
 *
 * Both channels are provided as regular files in the host system, which must be
 * mmap'ed by the guest application.
 *
 * Data channel
 * ------------
 *
 * The guest must mmap a file named <base>-data, where base is the argument
 * given to hypertrace_init.
 *
 * Regular memory accesses are used on the data channel.
 *
 * Control channel
 * ---------------
 *
 * The guest must mmap a file named <base>-control, where base is the argument
 * given to hypertrace_init.
 *
 * The first 64 bits of that memory contain the size of the data channel.
 *
 * The control channel is mprotect'ed by QEMU so that guest writes can be
 * intercepted by QEMU in order to raise the "guest_hypertrace" tracing
 * event. The guest must perform writes twice, one on each of two consecutive
 * pages, so that the written data can be both read by QEMU and the access
 * intercepted.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "qemu/osdep.h"
#include "cpu.h"

#include "hypertrace/user.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "trace.h"


static char *data_path = NULL;
static char *control_path = NULL;
static int data_fd = -1;
static int control_fd = -1;

static uint64_t *qemu_data = NULL;
static void *qemu_control_0 = NULL;
static void *qemu_control_1 = NULL;

static struct stat control_fd_stat;

struct sigaction segv_next;
static void segv_handler(int signum, siginfo_t *siginfo, void *sigctxt);


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
            .name = "pages",
            .type = QEMU_OPT_NUMBER,
            .def_value_str = "1",
        },
        { /* end of list */ }
    },
};

void hypertrace_opt_parse(const char *optarg, char **base, size_t *size)
{
    int pages;
    QemuOpts *opts = qemu_opts_parse_noisily(qemu_find_opts("hypertrace"),
                                             optarg, true);
    if (!opts) {
        exit(1);
    }
    if (qemu_opt_get(opts, "path")) {
        *base = g_strdup(qemu_opt_get(opts, "path"));
    } else {
        *base = NULL;
    }
    pages = qemu_opt_get_number(opts, "pages", 1);
    if (pages <= 0) {
        error_report("Parameter 'pages' expects a positive number");
        exit(EXIT_FAILURE);
    }
    *size = pages * TARGET_PAGE_SIZE;
}

static void init_channel(const char *base, const char *suffix, size_t size,
                         char ** path, int *fd, uint64_t **addr)
{
    *path = g_malloc(strlen(base) + strlen(suffix) + 1);
    sprintf(*path, "%s%s", base, suffix);

    *fd = open(*path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (*fd == -1) {
        error_report("error: open(%s): %s", *path, strerror(errno));
        abort();
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

static void fini_handler(int signum, siginfo_t *siginfo, void *sigctxt)
{
    hypertrace_fini();
}

void hypertrace_init(const char *base, uint64_t data_size)
{
    if (base == NULL) {
        return;
    }

    struct sigaction sigint;
    memset(&sigint, 0, sizeof(sigint));
    sigint.sa_sigaction = fini_handler;
    sigint.sa_flags = SA_SIGINFO | SA_RESTART;
    if (sigaction(SIGINT, &sigint, NULL) != 0) {
        error_report("error: sigaction(SIGINT): %s", strerror(errno));
        abort();
    }
    if (sigaction(SIGABRT, &sigint, NULL) != 0) {
        error_report("error: sigaction(SIGABRT): %s", strerror(errno));
        abort();
    }

    init_channel(base, "-data", data_size, &data_path, &data_fd, &qemu_data);
    uint64_t *control;
    init_channel(base, "-control", getpagesize() * 2, &control_path, &control_fd, &control);

    control[0] = tswap64(data_size / (CONFIG_HYPERTRACE_ARGS * sizeof(uint64_t)));

    if (fstat(control_fd, &control_fd_stat) == -1) {
        error_report("error: fstat(hypertrace_control): %s", strerror(errno));
        abort();
    }

    struct sigaction segv;
    memset(&segv, 0, sizeof(segv));
    segv.sa_sigaction = segv_handler;
    segv.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&segv.sa_mask);

    if (sigaction(SIGSEGV, &segv, &segv_next) != 0) {
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
    static bool atexit_in = false;
    if (atexit_in) {
        return;
    }
    atexit_in = true;

    if (sigaction(SIGSEGV, &segv_next, NULL) != 0) {
        error_report("error: sigaction(SIGSEGV): %s", strerror(errno));
        abort();
    }
    fini_channel(&data_fd, &data_path);
    fini_channel(&control_fd, &control_path);
}


void hypertrace_guest_mmap(int fd, void *qemu_addr)
{
    struct stat s;
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
    qemu_control_0 = qemu_addr;
    qemu_control_1 = qemu_control_0 + getpagesize();
    if (mprotect(qemu_control_0, getpagesize(), PROT_READ) == -1) {
        error_report("error: mprotect(hypertrace_control): %s", strerror(errno));
        abort();
    }
}

static void swap_control(void *from, void *to)
{
    if (mprotect(from, getpagesize(), PROT_READ | PROT_WRITE) == -1) {
        error_report("error: mprotect(from): %s", strerror(errno));
        abort();
    }
    if (mprotect(to, getpagesize(), PROT_READ) == -1) {
        error_report("error: mprotect(to): %s", strerror(errno));
        abort();
    }
}

#include "hypertrace/emit.c"

static void segv_handler(int signum, siginfo_t *siginfo, void *sigctxt)
{
    if (qemu_control_0 <= siginfo->si_addr &&
        siginfo->si_addr < qemu_control_1) {

        /* 1st fault (guest will write cmd) */
        assert(((unsigned long)siginfo->si_addr % getpagesize()) == sizeof(uint64_t));
        swap_control(qemu_control_0, qemu_control_1);

    } else if (qemu_control_1 <= siginfo->si_addr &&
               siginfo->si_addr < qemu_control_1 + getpagesize()) {
        uint64_t vcontrol = ((uint64_t*)qemu_control_0)[2];
        uint64_t *data_ptr = &qemu_data[vcontrol * CONFIG_HYPERTRACE_ARGS * sizeof(uint64_t)];

        /* 2nd fault (invoke) */
        assert(((unsigned long)siginfo->si_addr % getpagesize()) == sizeof(uint64_t));
        hypertrace_emit(current_cpu, data_ptr);
        swap_control(qemu_control_1, qemu_control_0);

    } else {
        /* proxy to next handler */
        if (segv_next.sa_sigaction != NULL) {
            segv_next.sa_sigaction(signum, siginfo, sigctxt);
        } else if (segv_next.sa_handler != NULL) {
            segv_next.sa_handler(signum);
        }
    }
}
