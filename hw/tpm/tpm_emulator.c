/*
 *  emulator TPM driver
 *
 *  Copyright (c) 2017 Intel Corporation
 *  Author: Amarnath Valluri <amarnath.valluri@intel.com>
 *
 *  Copyright (c) 2010 - 2013 IBM Corporation
 *  Authors:
 *    Stefan Berger <stefanb@us.ibm.com>
 *
 *  Copyright (C) 2011 IAIK, Graz University of Technology
 *    Author: Andreas Niederl
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "io/channel-socket.h"
#include "sysemu/tpm_backend.h"
#include "tpm_int.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "tpm_util.h"
#include "tpm_ioctl.h"
#include "qapi/error.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>

#define DEBUG_TPM 0

#define DPRINT(fmt, ...) do { \
    if (DEBUG_TPM) { \
        fprintf(stderr, fmt, ## __VA_ARGS__); \
    } \
} while (0);

#define DPRINTF(fmt, ...) DPRINT("tpm-emulator: "fmt"\n", __VA_ARGS__)

#define TYPE_TPM_EMULATOR "tpm-emulator"
#define TPM_EMULATOR(obj) \
    OBJECT_CHECK(TPMEmulator, (obj), TYPE_TPM_EMULATOR)

static const TPMDriverOps tpm_emulator_driver;

/* data structures */
typedef struct TPMEmulator {
    TPMBackend parent;

    TPMEmulatorOptions *ops;
    QIOChannel *data_ioc;
    QIOChannel *ctrl_ioc;
    bool op_executing;
    bool op_canceled;
    bool child_running;
    TPMVersion tpm_version;
    ptm_cap caps; /* capabilities of the TPM */
    uint8_t cur_locty_number; /* last set locality */
    QemuMutex state_lock;
} TPMEmulator;

#define TPM_DEFAULT_EMULATOR "swtpm"
#define TPM_DEFAULT_LOGLEVEL 5
#define TPM_EMULATOR_PIDFILE "/tmp/qemu-tpm.pid"
#define TPM_EMULATOR_IMPLEMENTS_ALL_CAPS(S, cap) (((S)->caps & (cap)) == (cap))

static int tpm_emulator_ctrlcmd(QIOChannel *ioc, unsigned long cmd, void *msg,
                                size_t msg_len_in, size_t msg_len_out)
{
    ssize_t n;

    uint32_t cmd_no = cpu_to_be32(cmd);
    struct iovec iov[2] = {
        { .iov_base = &cmd_no, .iov_len = sizeof(cmd_no), },
        { .iov_base = msg, .iov_len = msg_len_in, },
    };

    n = qio_channel_writev(ioc, iov, 2, NULL);
    if (n > 0) {
        if (msg_len_out > 0) {
            n = qio_channel_read(ioc, (char *)msg, msg_len_out, NULL);
            /* simulate ioctl return value */
            if (n > 0) {
                n = 0;
            }
        } else {
            n = 0;
        }
    }
    return n;
}

static int tpm_emulator_unix_tx_bufs(TPMEmulator *tpm_pt,
                                     const uint8_t *in, uint32_t in_len,
                                     uint8_t *out, uint32_t out_len,
                                     bool *selftest_done)
{
    ssize_t ret;
    bool is_selftest;
    const struct tpm_resp_hdr *hdr;

    if (!tpm_pt->child_running) {
        return -1;
    }

    tpm_pt->op_canceled = false;
    tpm_pt->op_executing = true;
    *selftest_done = false;

    is_selftest = tpm_util_is_selftest(in, in_len);

    ret = qio_channel_write(tpm_pt->data_ioc, (const char *)in, (size_t)in_len,
                            NULL);
    if (ret != in_len) {
        if (!tpm_pt->op_canceled || errno != ECANCELED) {
            error_report("tpm-emulator: error while transmitting data "
                         "to TPM: %s (%i)", strerror(errno), errno);
        }
        goto err_exit;
    }

    tpm_pt->op_executing = false;

    ret = qio_channel_read(tpm_pt->data_ioc, (char *)out, (size_t)out_len,
                           NULL);
    if (ret < 0) {
        if (!tpm_pt->op_canceled || errno != ECANCELED) {
            error_report("tpm-emulator: error while reading data from "
                         "TPM: %s (%i)", strerror(errno), errno);
        }
    } else if (ret < sizeof(struct tpm_resp_hdr) ||
               be32_to_cpu(((struct tpm_resp_hdr *)out)->len) != ret) {
        ret = -1;
        error_report("tpm-emulator: received invalid response "
                     "packet from TPM");
    }

    if (is_selftest && (ret >= sizeof(struct tpm_resp_hdr))) {
        hdr = (struct tpm_resp_hdr *)out;
        *selftest_done = (be32_to_cpu(hdr->errcode) == 0);
    }

err_exit:
    if (ret < 0) {
        tpm_util_write_fatal_error_response(out, out_len);
    }

    tpm_pt->op_executing = false;

    return ret;
}

static int tpm_emulator_set_locality(TPMEmulator *tpm_pt,
                                     uint8_t locty_number)
{
    ptm_loc loc;

    if (!tpm_pt->child_running) {
        return -1;
    }

    DPRINTF("%s : locality: 0x%x", __func__, locty_number);

    if (tpm_pt->cur_locty_number != locty_number) {
        DPRINTF("setting locality : 0x%x", locty_number);
        loc.u.req.loc = locty_number;
        if (tpm_emulator_ctrlcmd(tpm_pt->ctrl_ioc, CMD_SET_LOCALITY, &loc,
                             sizeof(loc), sizeof(loc)) < 0) {
            error_report("tpm-emulator: could not set locality : %s",
                         strerror(errno));
            return -1;
        }
        loc.u.resp.tpm_result = be32_to_cpu(loc.u.resp.tpm_result);
        if (loc.u.resp.tpm_result != 0) {
            error_report("tpm-emulator: TPM result for set locality : 0x%x",
                         loc.u.resp.tpm_result);
            return -1;
        }
        tpm_pt->cur_locty_number = locty_number;
    }
    return 0;
}

static void tpm_emulator_handle_request(TPMBackend *tb, TPMBackendCmd cmd)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);
    TPMLocality *locty = NULL;
    bool selftest_done = false;

    DPRINTF("processing command type %d", cmd);

    switch (cmd) {
    case TPM_BACKEND_CMD_PROCESS_CMD:
        qemu_mutex_lock(&tpm_pt->state_lock);
        locty = tb->tpm_state->locty_data;
        if (tpm_emulator_set_locality(tpm_pt,
                                      tb->tpm_state->locty_number) < 0) {
            tpm_util_write_fatal_error_response(locty->r_buffer.buffer,
                                           locty->r_buffer.size);
        } else {
            tpm_emulator_unix_tx_bufs(tpm_pt, locty->w_buffer.buffer,
                                  locty->w_offset, locty->r_buffer.buffer,
                                  locty->r_buffer.size, &selftest_done);
        }
        tb->recv_data_callback(tb->tpm_state, tb->tpm_state->locty_number,
                               selftest_done);
        qemu_mutex_unlock(&tpm_pt->state_lock);
        break;
    case TPM_BACKEND_CMD_INIT:
    case TPM_BACKEND_CMD_END:
    case TPM_BACKEND_CMD_TPM_RESET:
        /* nothing to do */
        break;
    }
}

/*
 * Gracefully shut down the external unixio TPM
 */
static void tpm_emulator_shutdown(TPMEmulator *tpm_pt)
{
    ptm_res res;

    if (!tpm_pt->child_running) {
        return;
    }

    if (tpm_emulator_ctrlcmd(tpm_pt->ctrl_ioc, CMD_SHUTDOWN, &res, 0,
                         sizeof(res)) < 0) {
        error_report("tpm-emulator: Could not cleanly shutdown the TPM: %s",
                     strerror(errno));
    } else if (res != 0) {
        error_report("tpm-emulator: TPM result for sutdown: 0x%x",
                     be32_to_cpu(res));
    }
}

static int tpm_emulator_probe_caps(TPMEmulator *tpm_pt)
{
    if (!tpm_pt->child_running) {
        return -1;
    }

    DPRINTF("%s", __func__);
    if (tpm_emulator_ctrlcmd(tpm_pt->ctrl_ioc, CMD_GET_CAPABILITY,
                         &tpm_pt->caps, 0, sizeof(tpm_pt->caps)) < 0) {
        error_report("tpm-emulator: probing failed : %s", strerror(errno));
        return -1;
    }

    tpm_pt->caps = be64_to_cpu(tpm_pt->caps);

    DPRINTF("capbilities : 0x%lx", tpm_pt->caps);

    return 0;
}

static int tpm_emulator_check_caps(TPMEmulator *tpm_pt)
{
    ptm_cap caps = 0;
    const char *tpm = NULL;

    /* check for min. required capabilities */
    switch (tpm_pt->tpm_version) {
    case TPM_VERSION_1_2:
        caps = PTM_CAP_INIT | PTM_CAP_SHUTDOWN | PTM_CAP_GET_TPMESTABLISHED |
               PTM_CAP_SET_LOCALITY;
        tpm = "1.2";
        break;
    case TPM_VERSION_2_0:
        caps = PTM_CAP_INIT | PTM_CAP_SHUTDOWN | PTM_CAP_GET_TPMESTABLISHED |
               PTM_CAP_SET_LOCALITY | PTM_CAP_RESET_TPMESTABLISHED;
        tpm = "2";
        break;
    case TPM_VERSION_UNSPEC:
        error_report("tpm-emulator: TPM version has not been set");
        return -1;
    }

    if (!TPM_EMULATOR_IMPLEMENTS_ALL_CAPS(tpm_pt, caps)) {
        error_report("tpm-emulator: TPM does not implement minimum set of "
                     "required capabilities for TPM %s (0x%x)", tpm, (int)caps);
        return -1;
    }

    return 0;
}

static int tpm_emulator_init_tpm(TPMEmulator *tpm_pt)
{
    ptm_init init;
    ptm_res res;

    if (!tpm_pt->child_running) {
        return -1;
    }

    DPRINTF("%s", __func__);
    if (tpm_emulator_ctrlcmd(tpm_pt->ctrl_ioc, CMD_INIT, &init, sizeof(init),
                         sizeof(init)) < 0) {
        error_report("tpm-emulator: could not send INIT: %s",
                     strerror(errno));
        return -1;
    }

    res = be32_to_cpu(init.u.resp.tpm_result);
    if (res) {
        error_report("tpm-emulator: TPM result for CMD_INIT: 0x%x", res);
        return -1;
    }

    return 0;
}

static int tpm_emulator_startup_tpm(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);

    DPRINTF("%s", __func__);

    tpm_emulator_init_tpm(tpm_pt) ;

    return 0;
}

static bool tpm_emulator_get_tpm_established_flag(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);
    ptm_est est;

    DPRINTF("%s", __func__);
    if (tpm_emulator_ctrlcmd(tpm_pt->ctrl_ioc, CMD_GET_TPMESTABLISHED, &est, 0,
                         sizeof(est)) < 0) {
        error_report("tpm-emulator: Could not get the TPM established flag: %s",
                     strerror(errno));
        return false;
    }
    DPRINTF("established flag: %0x", est.u.resp.bit);

    return (est.u.resp.bit != 0);
}

static int tpm_emulator_reset_tpm_established_flag(TPMBackend *tb,
                                                   uint8_t locty)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);
    ptm_reset_est reset_est;
    ptm_res res;

    /* only a TPM 2.0 will support this */
    if (tpm_pt->tpm_version == TPM_VERSION_2_0) {
        reset_est.u.req.loc = tpm_pt->cur_locty_number;

        if (tpm_emulator_ctrlcmd(tpm_pt->ctrl_ioc, CMD_RESET_TPMESTABLISHED,
                                 &reset_est, sizeof(reset_est),
                                 sizeof(reset_est)) < 0) {
            error_report("tpm-emulator: Could not reset the establishment bit: "
                          "%s", strerror(errno));
            return -1;
        }

        res = be32_to_cpu(reset_est.u.resp.tpm_result);
        if (res) {
            error_report("tpm-emulator: TPM result for rest establixhed flag: "
                         "0x%x", res);
            return -1;
        }
    }

    return 0;
}

static bool tpm_emulator_had_startup_error(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);

    return !tpm_pt->child_running;
}

static void tpm_emulator_cancel_cmd(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);
    ptm_res res;

    /*
     * As of Linux 3.7 the tpm_tis driver does not properly cancel
     * commands on all TPM manufacturers' TPMs.
     * Only cancel if we're busy so we don't cancel someone else's
     * command, e.g., a command executed on the host.
     */
    if (tpm_pt->op_executing) {
        if (TPM_EMULATOR_IMPLEMENTS_ALL_CAPS(tpm_pt, PTM_CAP_CANCEL_TPM_CMD)) {
            if (tpm_emulator_ctrlcmd(tpm_pt->ctrl_ioc, CMD_CANCEL_TPM_CMD, &res,
                                 0, sizeof(res)) < 0) {
                error_report("tpm-emulator: Could not cancel command: %s",
                             strerror(errno));
            } else if (res != 0) {
                error_report("tpm-emulator: Failed to cancel TPM: 0x%x",
                             be32_to_cpu(res));
            } else {
                tpm_pt->op_canceled = true;
            }
        }
    }
}

static void tpm_emulator_reset(TPMBackend *tb)
{
    DPRINTF("%s", __func__);

    tpm_emulator_cancel_cmd(tb);
}

static const char *tpm_emulator_desc(void)
{
    return "TPM emulator backend driver";
}

static TPMVersion tpm_emulator_get_tpm_version(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);

    return tpm_pt->tpm_version;
}

static gboolean tpm_emulator_fd_handler(QIOChannel *ioc, GIOCondition cnd,
                                        void *opaque)
{
    TPMEmulator *tpm_pt = opaque;

    if (cnd & G_IO_ERR || cnd & G_IO_HUP) {
        error_report("TPM backend disappeared");
        tpm_pt->child_running = false;
        return false;
    }

    return true;
}

static QIOChannel *_iochannel_new(const char *path, int fd, Error **err)
{
    int socket = path ?  unix_connect(path, err) : fd;
    if (socket < 0) {
        return NULL;
    }

    return QIO_CHANNEL(qio_channel_socket_new_fd(socket, err));
}

static int tpm_emulator_spawn_emulator(TPMEmulator *tpm_pt)
{
    int fds[2] = { -1, -1 };
    int ctrl_fds[2] = { -1, -1 };
    pid_t cpid;

    if (!tpm_pt->ops->has_data_path) {
        if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) < 0) {
            return -1;
        }
    }

    if (!tpm_pt->ops->has_ctrl_path) {
        if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ctrl_fds) < 0) {
            if (!tpm_pt->ops->has_data_path) {
                closesocket(fds[0]);
                closesocket(fds[1]);
            }
            return -1;
        }
    }

    cpid = qemu_fork(NULL);
    if (cpid < 0) {
        error_report("tpm-emulator: Fork failure: %s", strerror(errno));
        if (!tpm_pt->ops->has_data_path) {
            closesocket(fds[0]);
            closesocket(fds[1]);
        }
        if (!tpm_pt->ops->has_ctrl_path) {
            closesocket(ctrl_fds[0]);
            closesocket(ctrl_fds[1]);
        }
        return -1;
    }

    unlink(TPM_EMULATOR_PIDFILE);

    if (cpid == 0) { /* CHILD */
        enum {
            PARAM_PATH,
            PARAM_IFACE,
            PARAM_SERVER,  PARAM_SERVER_ARGS,
            PARAM_CTRL,    PARAM_CTRL_ARGS,
            PARAM_STATE,   PARAM_STATE_ARGS,
            PARAM_PIDFILE, PARAM_PIDFILE_ARGS,
            PARAM_LOG,     PARAM_LOG_ARGS,
            PARAM_MAX
        };

        int i;
        int data_fd = -1, ctrl_fd = -1;
        char *argv[PARAM_MAX + 1];

        /* close all unused inherited sockets */
        if (fds[0] >= 0) {
            closesocket(fds[0]);
        }
        if (ctrl_fds[0] >= 0) {
            closesocket(ctrl_fds[0]);
        }

        i = STDERR_FILENO + 1;
        if (fds[1] >= 0) {
            data_fd = dup2(fds[1], i++);
            if (data_fd < 0) {
                error_report("tpm-emulator: dup2() failure - %s",
                             strerror(errno));
                goto exit_child;
            }
        }
        if (ctrl_fds[1] >= 0) {
            ctrl_fd = dup2(ctrl_fds[1], i++);
            if (ctrl_fd < 0) {
                error_report("tpm-emulator: dup2() failure - %s",
                             strerror(errno));
                goto exit_child;
            }
        }
        for ( ; i < sysconf(_SC_OPEN_MAX); i++) {
            close(i);
        }

        argv[PARAM_MAX] = NULL;
        argv[PARAM_PATH] = g_strdup(tpm_pt->ops->path);
        argv[PARAM_IFACE] = g_strdup("socket");
        if (tpm_pt->ops->has_data_path) {
            argv[PARAM_SERVER] = g_strdup("--server");
            argv[PARAM_SERVER_ARGS] = g_strdup_printf("type=unixio,path=%s",
                                               tpm_pt->ops->data_path);
        } else {
            argv[PARAM_SERVER] = g_strdup("--fd");
            argv[PARAM_SERVER_ARGS] = g_strdup_printf("%d", data_fd);
        }

        argv[PARAM_CTRL] = g_strdup("--ctrl");
        if (tpm_pt->ops->has_ctrl_path) {
            argv[PARAM_CTRL_ARGS] = g_strdup_printf("type=unixio,path=%s",
                                                    tpm_pt->ops->ctrl_path);
        } else {
            argv[PARAM_CTRL_ARGS] = g_strdup_printf("type=unixio,clientfd=%d",
                                                    ctrl_fd);
        }

        argv[PARAM_STATE] = g_strdup("--tpmstate");
        argv[PARAM_STATE_ARGS] = g_strdup_printf("dir=%s",
                                        tpm_pt->ops->tpmstatedir);
        argv[PARAM_PIDFILE] = g_strdup("--pid");
        argv[PARAM_PIDFILE_ARGS] = g_strdup_printf("file=%s",
                                            TPM_EMULATOR_PIDFILE);
        if (tpm_pt->ops->has_logfile) {
            argv[PARAM_LOG] = g_strdup("--log");
            argv[PARAM_LOG_ARGS] = g_strdup_printf("file=%s,level=%d",
                    tpm_pt->ops->logfile, (int)tpm_pt->ops->loglevel);
        } else {
            /* truncate logs */
            argv[PARAM_LOG] = NULL;
        }
        DPRINTF("%s", "Running cmd: ")
        for (i = 0; argv[i]; i++) {
            DPRINT(" %s", argv[i])
        }
        DPRINT("\n")
        if (execv(tpm_pt->ops->path, (char * const *)argv) < 0) {
            error_report("execv() failure : %s", strerror(errno));
        }

exit_child:
        g_strfreev(argv);
        if (data_fd >= 0) {
            closesocket(data_fd);
        }
        if (ctrl_fd >= 0) {
            closesocket(ctrl_fd);
        }

        _exit(1);
    } else { /* self */
        struct stat st;
        DPRINTF("child pid: %d", cpid);
        int rc;
        useconds_t usec = 100 * 1000L; /* wait for 100 milliseconds */
        useconds_t timeout = 10; /* max 1 second */

        /* close unused sockets */
        if (fds[1] >= 0) {
            closesocket(fds[1]);
        }
        if (ctrl_fds[1] >= 0) {
            closesocket(ctrl_fds[1]);
        }

        tpm_pt->data_ioc = _iochannel_new(tpm_pt->ops->data_path, fds[0], NULL);
        if (!tpm_pt->data_ioc) {
            error_report("tpm-emulator: Unable to connect socket : %s",
                          tpm_pt->ops->data_path);
            goto err_kill_child;
        }

        tpm_pt->ctrl_ioc = _iochannel_new(tpm_pt->ops->ctrl_path, ctrl_fds[0],
                                          NULL);
        if (!tpm_pt->ctrl_ioc) {
            error_report("tpm-emulator: Unable to connect socket : %s",
                          tpm_pt->ops->ctrl_path);
            goto err_kill_child;
        }

        qemu_add_child_watch(cpid);

        qio_channel_add_watch(tpm_pt->data_ioc, G_IO_HUP | G_IO_ERR,
                              tpm_emulator_fd_handler, tpm_pt, NULL);

        while ((rc = stat(TPM_EMULATOR_PIDFILE, &st)) < 0 && timeout--) {
            usleep(usec);
        }

        if (timeout == -1) {
            error_report("tpm-emulator: pid file not ready: %s",
                         strerror(errno));
            goto err_kill_child;
        }

        /* check if child really running */
        if (kill(cpid, 0) < 0 && errno == ESRCH) {
            goto err_no_child;
        }

        tpm_pt->child_running = true;
    }

    return 0;

err_kill_child:
    kill(cpid, SIGTERM);
    /* wait for 10 mill-seconds */
    usleep(10 * 1000);
    /* force kill if still reachable */
    if (kill(cpid, 0) == 0) {
        kill(cpid, SIGKILL);
    }

err_no_child:
    tpm_pt->child_running = false;

    return -1;
}

static int tpm_emulator_handle_device_opts(TPMEmulator *tpm_pt, QemuOpts *opts)
{
    const char *value;

    value = qemu_opt_get(opts, "tpmstatedir");
    if (!value) {
        error_report("tpm-emulator: Missing tpm state directory");
        return -1;
    }
    tpm_pt->ops->tpmstatedir = g_strdup(value);

    tpm_pt->ops->spawn = qemu_opt_get_bool(opts, "spawn", false);

    value = qemu_opt_get(opts, "path");
    if (!value) {
        value = TPM_DEFAULT_EMULATOR;
        tpm_pt->ops->has_path = false;
    } else {
        tpm_pt->ops->has_path = true;
        if (value[0] == '/') {
            struct stat st;
            if (stat(value, &st) < 0 || !(S_ISREG(st.st_mode)
                || S_ISLNK(st.st_mode))) {
                error_report("tpm-emulator: Invalid emulator path: %s", value);
                return -1;
            }
        }
    }
    tpm_pt->ops->path = g_strdup(value);

    value = qemu_opt_get(opts, "data-path");
    if (value) {
        tpm_pt->ops->has_data_path = true;
        tpm_pt->ops->data_path = g_strdup(value);
    } else {
        tpm_pt->ops->has_data_path = false;
        if (!tpm_pt->ops->spawn) {
            error_report("tpm-emulator: missing mandatory data-path");
            return -1;
        }
    }

    value = qemu_opt_get(opts, "ctrl-path");
    if (value) {
        tpm_pt->ops->has_ctrl_path = true;
        tpm_pt->ops->ctrl_path = g_strdup(value);
    } else {
        tpm_pt->ops->has_ctrl_path = false;
        if (!tpm_pt->ops->spawn) {
            error_report("tpm-emulator: missing mandatory ctrl-path");
            return -1;
        }
    }

    value = qemu_opt_get(opts, "logfile");
    if (value) {
        tpm_pt->ops->has_logfile = true;
        tpm_pt->ops->logfile = g_strdup(value);
        tpm_pt->ops->loglevel = qemu_opt_get_number(opts, "loglevel",
                                                   TPM_DEFAULT_LOGLEVEL);
        tpm_pt->ops->has_loglevel = tpm_pt->ops->loglevel !=
                                     TPM_DEFAULT_LOGLEVEL;
    }

    if (tpm_pt->ops->spawn) {
        if (tpm_emulator_spawn_emulator(tpm_pt) < 0) {
            goto err_close_dev;
        }
    } else {
        tpm_pt->data_ioc = _iochannel_new(tpm_pt->ops->data_path, -1, NULL);
        if (tpm_pt->data_ioc  == NULL) {
            error_report("tpm-emulator: Failed to connect data socket: %s",
                         tpm_pt->ops->data_path);
            goto err_close_dev;
        }
        tpm_pt->ctrl_ioc = _iochannel_new(tpm_pt->ops->ctrl_path, -1, NULL);
        if (tpm_pt->ctrl_ioc == NULL) {
            DPRINTF("Failed to connect control socket: %s",
                    strerror(errno));
            goto err_close_dev;
        }
        tpm_pt->child_running = true;
    }

    /* FIXME: tpm_util_test_tpmdev() accepts only on socket fd, as it also used
     * by passthrough driver, which not yet using GIOChannel.
     */
    if (tpm_util_test_tpmdev(QIO_CHANNEL_SOCKET(tpm_pt->data_ioc)->fd,
                             &tpm_pt->tpm_version)) {
        error_report("'%s' is not emulating TPM device.", tpm_pt->ops->path);
        goto err_close_dev;
    }

    DPRINTF("TPM Version %s", tpm_pt->tpm_version == TPM_VERSION_1_2 ? "1.2" :
             (tpm_pt->tpm_version == TPM_VERSION_2_0 ?  "2.0" : "Unspecified"));

    if (tpm_emulator_probe_caps(tpm_pt) ||
        tpm_emulator_check_caps(tpm_pt)) {
        goto err_close_dev;
    }

    return 0;

err_close_dev:
    DPRINT("Startup error\n");
    return -1;
}

static TPMBackend *tpm_emulator_create(QemuOpts *opts, const char *id)
{
    TPMBackend *tb = TPM_BACKEND(object_new(TYPE_TPM_EMULATOR));

    tb->id = g_strdup(id);

    if (tpm_emulator_handle_device_opts(TPM_EMULATOR(tb), opts)) {
        goto err_exit;
    }

    return tb;

err_exit:
    object_unref(OBJECT(tb));

    return NULL;
}

static TPMOptions *tpm_emulator_get_tpm_options(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);
    TPMEmulatorOptions *ops = g_new0(TPMEmulatorOptions, 1);

    if (!ops) {
        return NULL;
    }
    DPRINTF("%s", __func__);

    ops->tpmstatedir = g_strdup(tpm_pt->ops->tpmstatedir);
    ops->spawn = tpm_pt->ops->spawn;
    if (tpm_pt->ops->has_path) {
        ops->has_path = true;
        ops->path = g_strdup(tpm_pt->ops->path);
    }
    if (tpm_pt->ops->has_data_path) {
        ops->has_data_path = true;
        ops->data_path = g_strdup(tpm_pt->ops->data_path);
    }
    if (tpm_pt->ops->has_ctrl_path) {
        ops->has_ctrl_path = true;
        ops->ctrl_path = g_strdup(tpm_pt->ops->ctrl_path);
    }
    if (tpm_pt->ops->has_logfile) {
        ops->has_logfile = true;
        ops->logfile = g_strdup(tpm_pt->ops->logfile);
    }
    if (tpm_pt->ops->has_loglevel) {
        ops->has_loglevel = true;
        ops->loglevel = tpm_pt->ops->loglevel;
    }

    return (TPMOptions *)ops;
}

static const QemuOptDesc tpm_emulator_cmdline_opts[] = {
    TPM_STANDARD_CMDLINE_OPTS,
    {
        .name = "tpmstatedir",
        .type = QEMU_OPT_STRING,
        .help = "TPM state directroy",
    },
    {
        .name = "spawn",
        .type = QEMU_OPT_BOOL,
        .help = "Wether to spwan given emlatory binary",
    },
    {
        .name = "path",
        .type = QEMU_OPT_STRING,
        .help = "Path to TPM emulator binary",
    },
    {
        .name = "data-path",
        .type = QEMU_OPT_STRING,
        .help = "Socket path to use for data exhange",
    },
    {
        .name = "ctrl-path",
        .type = QEMU_OPT_STRING,
        .help = "Socket path to use for out-of-band control messages",
    },
    {
        .name = "logfile",
        .type = QEMU_OPT_STRING,
        .help = "Path to log file",
    },
    {
        .name = "level",
        .type = QEMU_OPT_STRING,
        .help = "Log level number",
    },
    { /* end of list */ },
};

static const TPMDriverOps tpm_emulator_driver = {
    .type                     = TPM_TYPE_EMULATOR,
    .opts                     = tpm_emulator_cmdline_opts,
    .desc                     = tpm_emulator_desc,
    .create                   = tpm_emulator_create,
    .startup_tpm              = tpm_emulator_startup_tpm,
    .reset                    = tpm_emulator_reset,
    .had_startup_error        = tpm_emulator_had_startup_error,
    .cancel_cmd               = tpm_emulator_cancel_cmd,
    .get_tpm_established_flag = tpm_emulator_get_tpm_established_flag,
    .reset_tpm_established_flag = tpm_emulator_reset_tpm_established_flag,
    .get_tpm_version          = tpm_emulator_get_tpm_version,
    .get_tpm_options          = tpm_emulator_get_tpm_options,
};

static void tpm_emulator_inst_init(Object *obj)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(obj);

    DPRINTF("%s", __func__);
    tpm_pt->ops = g_new0(TPMEmulatorOptions, 1);
    tpm_pt->data_ioc = tpm_pt->ctrl_ioc = NULL;
    tpm_pt->op_executing = tpm_pt->op_canceled = false;
    tpm_pt->child_running = false;
    tpm_pt->cur_locty_number = ~0;
    qemu_mutex_init(&tpm_pt->state_lock);
}

static void tpm_emulator_inst_finalize(Object *obj)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(obj);

    tpm_emulator_cancel_cmd(TPM_BACKEND(obj));
    tpm_emulator_shutdown(tpm_pt);

    if (tpm_pt->data_ioc) {
        qio_channel_close(tpm_pt->data_ioc, NULL);
    }
    if (tpm_pt->ctrl_ioc) {
        qio_channel_close(tpm_pt->ctrl_ioc, NULL);
    }
    if (tpm_pt->ops) {
        qapi_free_TPMEmulatorOptions(tpm_pt->ops);
    }
}

static void tpm_emulator_class_init(ObjectClass *klass, void *data)
{
    TPMBackendClass *tbc = TPM_BACKEND_CLASS(klass);
    tbc->ops = &tpm_emulator_driver;
    tbc->handle_request = tpm_emulator_handle_request;
}

static const TypeInfo tpm_emulator_info = {
    .name = TYPE_TPM_EMULATOR,
    .parent = TYPE_TPM_BACKEND,
    .instance_size = sizeof(TPMEmulator),
    .class_init = tpm_emulator_class_init,
    .instance_init = tpm_emulator_inst_init,
    .instance_finalize = tpm_emulator_inst_finalize,
};

static void tpm_emulator_register(void)
{
    type_register_static(&tpm_emulator_info);
    tpm_register_driver(&tpm_emulator_driver);
}

type_init(tpm_emulator_register)
