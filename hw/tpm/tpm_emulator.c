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
 * The origin of the code is from CUSE driver posed by Stefan Berger:
 *    https://github.com/stefanberger/qemu-tpm
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/tpm_backend.h"
#include "tpm_int.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "sysemu/tpm_backend_int.h"
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

#define DPRINTF(fmt, ...) DPRINT(fmt"\n", __VA_ARGS__)

#define TYPE_TPM_EMULATOR "emulator"
#define TPM_EMULATOR(obj) \
    OBJECT_CHECK(TPMEmulator, (obj), TYPE_TPM_EMULATOR)

static const TPMDriverOps tpm_emulator_driver;

/* data structures */
typedef struct TPMEmulator {
    TPMBackend parent;

    TPMEmulatorOptions ops;
    int tpm_fd;
    int tpm_ctrl_fd;
    bool op_executing;
    bool op_canceled;
    bool child_running;
    TPMVersion tpm_version;
    ptm_cap caps; /* capabilities of the TPM */
    uint8_t cur_locty_number; /* last set locality */
} TPMEmulator;

#define TPM_DEFAULT_EMULATOR "swtpm"
#define TPM_DEFAULT_LOGLEVEL 5
#define TPM_EUMLATOR_IMPLEMENTS_ALL_CAPS(S, cap) (((S)->caps & (cap)) == (cap))

static int tpm_emulator_unix_tx_bufs(TPMEmulator *tpm_pt,
                                     const uint8_t *in, uint32_t in_len,
                                     uint8_t *out, uint32_t out_len,
                                     bool *selftest_done)
{
    int ret;
    bool is_selftest;
    const struct tpm_resp_hdr *hdr;

    if (!tpm_pt->child_running) {
        return -1;
    }

    tpm_pt->op_canceled = false;
    tpm_pt->op_executing = true;
    *selftest_done = false;

    is_selftest = tpm_util_is_selftest(in, in_len);

    ret = tpm_util_unix_write(tpm_pt->tpm_fd, in, in_len);
    if (ret != in_len) {
        if (!tpm_pt->op_canceled || errno != ECANCELED) {
            error_report("tpm_emulator: error while transmitting data "
                         "to TPM: %s (%i)", strerror(errno), errno);
        }
        goto err_exit;
    }

    tpm_pt->op_executing = false;

    ret = tpm_util_unix_read(tpm_pt->tpm_fd, out, out_len);
    if (ret < 0) {
        if (!tpm_pt->op_canceled || errno != ECANCELED) {
            error_report("tpm_emulator: error while reading data from "
                         "TPM: %s (%i)", strerror(errno), errno);
        }
    } else if (ret < sizeof(struct tpm_resp_hdr) ||
               be32_to_cpu(((struct tpm_resp_hdr *)out)->len) != ret) {
        ret = -1;
        error_report("tpm_emulator: received invalid response "
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

    DPRINTF("tpm_emulator: %s : locality: 0x%x", __func__, locty_number);

    if (tpm_pt->cur_locty_number != locty_number) {
        DPRINTF("tpm-emulator: setting locality : 0x%x", locty_number);
        loc.u.req.loc = cpu_to_be32(locty_number);
        if (tpm_util_ctrlcmd(tpm_pt->tpm_ctrl_fd, PTM_SET_LOCALITY, &loc,
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

    DPRINTF("tpm_emulator: processing command type %d", cmd);

    switch (cmd) {
    case TPM_BACKEND_CMD_PROCESS_CMD:
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

    if (tpm_util_ctrlcmd(tpm_pt->tpm_ctrl_fd, PTM_SHUTDOWN, &res, 0,
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

    DPRINTF("tpm_emulator: %s", __func__);
    if (tpm_util_ctrlcmd(tpm_pt->tpm_ctrl_fd, PTM_GET_CAPABILITY,
                         &tpm_pt->caps, 0, sizeof(tpm_pt->caps)) < 0) {
        error_report("tpm-emulator: probing failed : %s", strerror(errno));
        return -1;
    }

    tpm_pt->caps = be64_to_cpu(tpm_pt->caps);

    DPRINTF("tpm-emulator: capbilities : 0x%lx", tpm_pt->caps);

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

    if (!TPM_EUMLATOR_IMPLEMENTS_ALL_CAPS(tpm_pt, caps)) {
        error_report("tpm-emulator: TPM does not implement minimum set of "
                     "required capabilities for TPM %s (0x%x)", tpm, (int)caps);
        return -1;
    }

    return 0;
}

static int tpm_emulator_init_tpm(TPMEmulator *tpm_pt, bool is_resume)
{
    ptm_init init;
    ptm_res res;

    if (!tpm_pt->child_running) {
        return -1;
    }

    DPRINTF("tpm_emulator: %s", __func__);
    if (is_resume) {
        init.u.req.init_flags = cpu_to_be32(PTM_INIT_FLAG_DELETE_VOLATILE);
    }

    if (tpm_util_ctrlcmd(tpm_pt->tpm_ctrl_fd, PTM_INIT, &init, sizeof(init),
                         sizeof(init)) < 0) {
        error_report("tpm-emulator: could not send INIT: %s",
                     strerror(errno));
        return -1;
    }

    res = be32_to_cpu(init.u.resp.tpm_result);
    if (res) {
        error_report("tpm-emulator: TPM result for PTM_INIT: 0x%x", res);
        return -1;
    }

    return 0;
}

static int tpm_emulator_startup_tpm(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);

    DPRINTF("tpm_emulator: %s", __func__);

    tpm_emulator_init_tpm(tpm_pt, false) ;

    return 0;
}

static bool tpm_emulator_get_tpm_established_flag(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);
    ptm_est est;

    DPRINTF("tpm_emulator: %s", __func__);
    if (tpm_util_ctrlcmd(tpm_pt->tpm_ctrl_fd, PTM_GET_TPMESTABLISHED, &est, 0,
                         sizeof(est)) < 0) {
        error_report("tpm-emulator: Could not get the TPM established flag: %s",
                     strerror(errno));
        return false;
    }
    DPRINTF("tpm_emulator: established flag: %0x", est.u.resp.bit);

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
        reset_est.u.req.loc = cpu_to_be32(tpm_pt->cur_locty_number);

        if (tpm_util_ctrlcmd(tpm_pt->tpm_ctrl_fd, PTM_RESET_TPMESTABLISHED,
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

static bool tpm_emulator_get_startup_error(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);

    return !tpm_pt->child_running;
}

static size_t tpm_emulator_realloc_buffer(TPMSizedBuffer *sb)
{
    size_t wanted_size = 4096; /* Linux tpm.c buffer size */

    if (sb->size != wanted_size) {
        sb->buffer = g_realloc(sb->buffer, wanted_size);
        sb->size = wanted_size;
    }
    return sb->size;
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
        if (TPM_EUMLATOR_IMPLEMENTS_ALL_CAPS(tpm_pt, PTM_CAP_CANCEL_TPM_CMD)) {
            if (tpm_util_ctrlcmd(tpm_pt->tpm_ctrl_fd, PTM_CANCEL_TPM_CMD, &res,
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
    DPRINTF("tpm_emulator: %s", __func__);

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

static void tpm_emulator_fd_handler(void *opaque)
{
    TPMEmulator *tpm_pt = opaque;
    char val = 0;
    ssize_t size;

    qemu_set_fd_handler(tpm_pt->tpm_fd, NULL, NULL, NULL);

    size = qemu_recv(tpm_pt->tpm_fd, &val, 1, MSG_PEEK);
    if (!size) {
        error_report("TPM backend disappeared");
        tpm_pt->child_running = false;
    } else {
        DPRINT("tpm-emulator: unexpected data on TPM\n");
    }
}

static int tpm_emulator_spawn_emulator(TPMEmulator *tpm_pt)
{
    int fds[2];
    int ctrl_fds[2];
    pid_t cpid;

    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) < 0) {
        return -1;
    }

    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ctrl_fds) < 0) {
        closesocket(fds[0]);
        closesocket(fds[1]);
        return -1;
    }

    cpid = fork();
    if (cpid < 0) {
        error_report("tpm-emulator: Fork failure: %s", strerror(errno));
        closesocket(fds[0]); closesocket(fds[1]);
        closesocket(ctrl_fds[0]); closesocket(ctrl_fds[1]);
        return -1;
    }

    if (cpid == 0) { /* CHILD */
        int i;
        char fd_str[128] = "";
        char ctrl_fd_str[128] = "";
        char tpmstate_str[1024] = "";
        char log_str[1024] = "";
        const char *params[] = {
            tpm_pt->ops.path, "socket",
            "--fd", fd_str,
            "--ctrl", ctrl_fd_str,
            "--tpmstate", tpmstate_str,
            "--log", log_str,
            NULL /* End */
        };

        /* close all unused inherited sockets */
        closesocket(fds[0]);
        closesocket(ctrl_fds[0]);
        for (i = STDERR_FILENO + 1; i < fds[1]; i++) {
            closesocket(i);
        }

        sprintf(fd_str, "%d", fds[1]);
        sprintf(ctrl_fd_str, "type=unixio,clientfd=%d", ctrl_fds[1]);
        sprintf(tpmstate_str, "dir=%s", tpm_pt->ops.tpmstatedir);
        if (tpm_pt->ops.has_logfile) {
            sprintf(log_str, "file=%s,level=%d", tpm_pt->ops.logfile,
                    (int)tpm_pt->ops.loglevel);
        } else {
            /* truncate logs */
            params[8] = NULL;
        }
        DPRINT("Running cmd: ")
        for (i = 0; params[i]; i++) {
            DPRINT(" %s", params[i])
        }
        DPRINT("\n")
        if (execv(tpm_pt->ops.path, (char * const *)params) < 0) {
            error_report("execv() failure : %s", strerror(errno));
        }
        closesocket(fds[1]);
        closesocket(ctrl_fds[1]);
        exit(0);
    } else { /* self */
        DPRINTF("tpm-emulator: child pid: %d", cpid);
        /* FIXME: find better way of finding swtpm ready
                  maybe write 'ready'bit on socket ?
           give some time to child to get ready */
        sleep(1);

        tpm_pt->tpm_fd = fds[0];
        tpm_pt->tpm_ctrl_fd = ctrl_fds[0];
        tpm_pt->child_running = true;

        qemu_add_child_watch(cpid);

        fcntl(tpm_pt->tpm_fd, F_SETFL, O_NONBLOCK);
        qemu_set_fd_handler(tpm_pt->tpm_fd, tpm_emulator_fd_handler, NULL,
                            tpm_pt);

        /* close unsed sockets */
        closesocket(fds[1]);
        closesocket(ctrl_fds[1]);
    }

    return 0;
}

static int tpm_emulator_handle_device_opts(TPMEmulator *tpm_pt, QemuOpts *opts)
{
    const char *value;

    value = qemu_opt_get(opts, "tpmstatedir");
    if (!value) {
        error_report("tpm-emulator: Missing tpm state directory");
        return -1;
    }
    tpm_pt->ops.tpmstatedir = g_strdup(value);

    value = qemu_opt_get(opts, "path");
    if (!value) {
        value = TPM_DEFAULT_EMULATOR;
        tpm_pt->ops.has_path = false;
    } else {
        tpm_pt->ops.has_path = true;
        if (value[0] == '/') {
            struct stat st;
            if (stat(value, &st) < 0 || !(S_ISREG(st.st_mode)
                || S_ISLNK(st.st_mode))) {
                error_report("tpm-emulator: Invalid emulator path: %s", value);
                return -1;
            }
        }
    }
    tpm_pt->ops.path = g_strdup(value);

    value = qemu_opt_get(opts, "logfile");
    if (value) {
        DPRINTF("tpm-emulator: LogFile: %s", value);
        tpm_pt->ops.has_logfile = true;
        tpm_pt->ops.logfile = g_strdup(value);
        tpm_pt->ops.loglevel = qemu_opt_get_number(opts, "loglevel",
                                                   TPM_DEFAULT_LOGLEVEL);
        tpm_pt->ops.has_loglevel = tpm_pt->ops.loglevel !=
                                     TPM_DEFAULT_LOGLEVEL;
    }

    if (tpm_emulator_spawn_emulator(tpm_pt) < 0) {
        goto err_close_dev;
    }

    tpm_pt->cur_locty_number = ~0;

    if (tpm_emulator_probe_caps(tpm_pt) ||
        tpm_emulator_init_tpm(tpm_pt, false)) {
        goto err_close_dev;
    }

    if (tpm_util_test_tpmdev(tpm_pt->tpm_fd, &tpm_pt->tpm_version)) {
        error_report("'%s' is not emulating TPM device.", tpm_pt->ops.path);
        goto err_close_dev;
    }

    DPRINTF("tpm_emulator: TPM Version %s",
             tpm_pt->tpm_version == TPM_VERSION_1_2 ? "1.2" :
             (tpm_pt->tpm_version == TPM_VERSION_2_0 ?  "2.0" : "Unspecified"));

    if (tpm_emulator_check_caps(tpm_pt)) {
        goto err_close_dev;
    }

    return 0;

err_close_dev:
    tpm_emulator_shutdown(tpm_pt);
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

static void tpm_emulator_destroy(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);

    DPRINTF("tpm_emulator: %s", __func__);

    tpm_emulator_cancel_cmd(tb);
    tpm_emulator_shutdown(tpm_pt);

    closesocket(tpm_pt->tpm_fd);
    closesocket(tpm_pt->tpm_ctrl_fd);
    g_free(tpm_pt->ops.tpmstatedir);
    g_free(tpm_pt->ops.path);
    g_free(tpm_pt->ops.logfile);
}

static TPMOptions *tpm_emulator_get_tpm_options(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);
    TPMEmulatorOptions *ops = g_new(TPMEmulatorOptions, 1);

    if (!ops) {
        return NULL;
    }
    DPRINTF("tpm_emulator: %s", __func__);

    ops->tpmstatedir = g_strdup(tpm_pt->ops.tpmstatedir);
    if (tpm_pt->ops.has_path) {
        ops->has_path = true;
        ops->path = g_strdup(tpm_pt->ops.path);
    }
    if (tpm_pt->ops.has_logfile) {
        ops->has_logfile = true;
        ops->logfile = g_strdup(tpm_pt->ops.logfile);
    }
    if (tpm_pt->ops.has_loglevel) {
        ops->has_loglevel = true;
        ops->loglevel = tpm_pt->ops.loglevel;
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
        .name = "path",
        .type = QEMU_OPT_STRING,
        .help = "Path to TPM emulator binary",
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
    .destroy                  = tpm_emulator_destroy,
    .startup_tpm              = tpm_emulator_startup_tpm,
    .realloc_buffer           = tpm_emulator_realloc_buffer,
    .reset                    = tpm_emulator_reset,
    .had_startup_error        = tpm_emulator_get_startup_error,
    .cancel_cmd               = tpm_emulator_cancel_cmd,
    .get_tpm_established_flag = tpm_emulator_get_tpm_established_flag,
    .reset_tpm_established_flag = tpm_emulator_reset_tpm_established_flag,
    .get_tpm_version          = tpm_emulator_get_tpm_version,
    .get_tpm_options          = tpm_emulator_get_tpm_options,
};

static void tpm_emulator_inst_init(Object *obj)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(obj);

    DPRINTF("tpm_emulator: %s", __func__);
    tpm_pt->tpm_fd = tpm_pt->tpm_ctrl_fd = -1;
    tpm_pt->op_executing = tpm_pt->op_canceled = false;
    tpm_pt->child_running = false;
    tpm_pt->cur_locty_number = ~0;
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
};

static void tpm_emulator_register(void)
{
    type_register_static(&tpm_emulator_info);
    tpm_register_driver(&tpm_emulator_driver);
}

type_init(tpm_emulator_register)
