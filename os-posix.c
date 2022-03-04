/*
 * os-posix.c
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>

#include "qemu-common.h"
/* Needed early for CONFIG_BSD etc. */
#include "net/slirp.h"
#include "qemu/qemu-options.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "sysemu/runstate.h"
#include "qemu/cutils.h"

#ifdef CONFIG_LINUX
#include <sys/prctl.h>
#endif

static int daemonize;
static int daemon_pipe;

void os_setup_early_signal_handling(void)
{
    struct sigaction act;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &act, NULL);
}

static void termsig_handler(int signal, siginfo_t *info, void *c)
{
    qemu_system_killed(info->si_signo, info->si_pid);
}

void os_setup_signal_handling(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = termsig_handler;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGINT,  &act, NULL);
    sigaction(SIGHUP,  &act, NULL);
    sigaction(SIGTERM, &act, NULL);
}

void os_set_proc_name(const char *s)
{
#if defined(PR_SET_NAME)
    char name[16];
    if (!s)
        return;
    pstrcpy(name, sizeof(name), s);
    /* Could rewrite argv[0] too, but that's a bit more complicated.
       This simple way is enough for `top'. */
    if (prctl(PR_SET_NAME, name)) {
        error_report("unable to change process name: %s", strerror(errno));
        exit(1);
    }
#else
    error_report("Change of process name not supported by your OS");
    exit(1);
#endif
}


static void change_process_uid(uid_t uid, gid_t gid, const char *name)
{
    if (setgid(gid) < 0) {
        error_report("Failed to setgid(%d)", gid);
        exit(1);
    }
    if (name) {
        if (initgroups(name, gid) < 0) {
            error_report("Failed to initgroups(\"%s\", %d)",
                         name, gid);
            exit(1);
        }
    } else {
        if (setgroups(1, &gid) < 0) {
            error_report("Failed to setgroups(1, [%d])",
                         gid);
            exit(1);
        }
    }
    if (setuid(uid) < 0) {
        error_report("Failed to setuid(%d)", uid);
        exit(1);
    }
    if (setuid(0) != -1) {
        error_report("Dropping privileges failed");
        exit(1);
    }
}

static void change_root(const char *root)
{
    if (chroot(root) < 0) {
        error_report("chroot failed");
        exit(1);
    }
    if (chdir("/")) {
        error_report("not able to chdir to /: %s", strerror(errno));
        exit(1);
    }
}

void os_daemonize(void)
{
    pid_t pid;
    int fds[2];

    if (pipe(fds) == -1) {
        exit(1);
    }

    pid = fork();
    if (pid > 0) {
        uint8_t status;
        ssize_t len;

        close(fds[1]);

        do {
            len = read(fds[0], &status, 1);
        } while (len < 0 && errno == EINTR);

        /* only exit successfully if our child actually wrote
         * a one-byte zero to our pipe, upon successful init */
        exit(len == 1 && status == 0 ? 0 : 1);

    } else if (pid < 0) {
        exit(1);
    }

    close(fds[0]);
    daemon_pipe = fds[1];
    qemu_set_cloexec(daemon_pipe);

    setsid();

    pid = fork();
    if (pid > 0) {
            exit(0);
    } else if (pid < 0) {
        exit(1);
    }
    umask(027);

    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    daemonize = true;
}

void os_setup_post(const char *root_dir,
                   uid_t runas_uid, gid_t runas_gid,
                   const char *runas_name)
{
    int fd = 0;

    if (daemonize) {
        if (chdir("/")) {
            error_report("not able to chdir to /: %s", strerror(errno));
            exit(1);
        }
        TFR(fd = qemu_open_old("/dev/null", O_RDWR));
        if (fd == -1) {
            exit(1);
        }
    }

    if (root_dir != NULL) {
        change_root(root_dir);
    }
    if (runas_uid != -1 && runas_gid != -1) {
        change_process_uid(runas_uid, runas_gid, runas_name);
    }

    if (daemonize) {
        uint8_t status = 0;
        ssize_t len;

        dup2(fd, 0);
        dup2(fd, 1);
        /* In case -D is given do not redirect stderr to /dev/null */
        if (!qemu_logfile) {
            dup2(fd, 2);
        }

        close(fd);

        do {        
            len = write(daemon_pipe, &status, 1);
        } while (len < 0 && errno == EINTR);
        if (len != 1) {
            exit(1);
        }
    }
}

void os_set_line_buffering(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
}

int os_mlock(void)
{
#ifdef HAVE_MLOCKALL
    int ret = 0;

    ret = mlockall(MCL_CURRENT | MCL_FUTURE);
    if (ret < 0) {
        error_report("mlockall: %s", strerror(errno));
    }

    return ret;
#else
    return -ENOSYS;
#endif
}
