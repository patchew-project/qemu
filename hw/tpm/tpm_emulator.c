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
#include "migration/blocker.h"
#include "qapi/error.h"
#include "chardev/char-fe.h"

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

#define TPM_EMULATOR_IMPLEMENTS_ALL_CAPS(S, cap) (((S)->caps & (cap)) == (cap))

static const TPMDriverOps tpm_emulator_driver;

/* data structures */
typedef struct TPMEmulator {
    TPMBackend parent;

    TPMEmulatorOptions *options;
    CharBackend ctrl_dev;
    QIOChannel *data_ioc;
    bool op_executing;
    bool op_canceled;
    bool had_startup_error;
    TPMVersion tpm_version;
    ptm_cap caps; /* capabilities of the TPM */
    uint8_t cur_locty_number; /* last set locality */
    QemuMutex state_lock;
    Error *migration_blocker;
} TPMEmulator;


static int tpm_emulator_ctrlcmd(CharBackend *dev, unsigned long cmd, void *msg,
                                size_t msg_len_in, size_t msg_len_out)
{
    uint32_t cmd_no = cpu_to_be32(cmd);
    ssize_t n = sizeof(uint32_t) + msg_len_in;
    uint8_t *buf = NULL;

    buf = (uint8_t *)malloc(n);
    memcpy(buf, &cmd_no, sizeof(cmd_no));
    memcpy(buf + sizeof(cmd_no), msg, msg_len_in);

    n += qemu_chr_fe_write_all(dev, (const uint8_t *)buf, n);
    free(buf);

    if (n > 0) {
        if (msg_len_out > 0) {
            n = qemu_chr_fe_read_all(dev, (uint8_t *)msg, msg_len_out);
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
    bool is_selftest = false;
    const struct tpm_resp_hdr *hdr = NULL;
    Error *err = NULL;

    tpm_pt->op_canceled = false;
    tpm_pt->op_executing = true;
    if (selftest_done) {
        *selftest_done = false;
        is_selftest = tpm_util_is_selftest(in, in_len);
    }

    ret = qio_channel_write(tpm_pt->data_ioc, (char *)in, in_len, &err);
    if (ret != in_len || err) {
        if (!tpm_pt->op_canceled || errno != ECANCELED) {
            error_report("tpm-emulator: error while transmitting data "
                         "to TPM: %s", err ? error_get_pretty(err) : "");
            error_free(err);
        }
        goto err_exit;
    }

    tpm_pt->op_executing = false;

    ret = qio_channel_read(tpm_pt->data_ioc, (char *)out, out_len, &err);
    if (ret < 0 || err) {
        if (!tpm_pt->op_canceled || errno != ECANCELED) {
            error_report("tpm-emulator: error while reading data from "
                         "TPM: %s", err ? error_get_pretty(err) : "");
            error_free(err);
        }
    } else if (ret >= sizeof(*hdr)) {
        hdr = (struct tpm_resp_hdr *)out;
    }

    if (!hdr || be32_to_cpu(hdr->len) != ret) {
        error_report("tpm-emulator: received invalid response "
                     "packet from TPM with length :%ld", ret);
        ret = -1;
        goto err_exit;
    }

    if (is_selftest) {
        *selftest_done = (be32_to_cpu(hdr->errcode) == 0);
    }

    return 0;

err_exit:
    if (ret < 0) {
        tpm_util_write_fatal_error_response(out, out_len);
    }

    tpm_pt->op_executing = false;

    return ret;
}

static int tpm_emulator_set_locality(TPMEmulator *tpm_pt, uint8_t locty_number)
{
    ptm_loc loc;

    DPRINTF("%s : locality: 0x%x", __func__, locty_number);

    if (tpm_pt->cur_locty_number != locty_number) {
        DPRINTF("setting locality : 0x%x", locty_number);
        loc.u.req.loc = locty_number;
        if (tpm_emulator_ctrlcmd(&tpm_pt->ctrl_dev, CMD_SET_LOCALITY, &loc,
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
                                              locty->w_offset,
                                              locty->r_buffer.buffer,
                                              locty->r_buffer.size,
                                              &selftest_done);
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

    if (tpm_emulator_ctrlcmd(&tpm_pt->ctrl_dev, CMD_SHUTDOWN, &res, 0,
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
    DPRINTF("%s", __func__);
    if (tpm_emulator_ctrlcmd(&tpm_pt->ctrl_dev, CMD_GET_CAPABILITY,
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

static int tpm_emulator_startup_tpm(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);
    ptm_init init;
    ptm_res res;

    DPRINTF("%s", __func__);
    if (tpm_emulator_ctrlcmd(&tpm_pt->ctrl_dev, CMD_INIT, &init, sizeof(init),
                         sizeof(init)) < 0) {
        error_report("tpm-emulator: could not send INIT: %s",
                     strerror(errno));
        goto err_exit;
    }

    res = be32_to_cpu(init.u.resp.tpm_result);
    if (res) {
        error_report("tpm-emulator: TPM result for CMD_INIT: 0x%x", res);
        goto err_exit;
    }
    return 0;

err_exit:
    tpm_pt->had_startup_error = true;
    return -1;
}

static bool tpm_emulator_get_tpm_established_flag(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);
    ptm_est est;

    DPRINTF("%s", __func__);
    if (tpm_emulator_ctrlcmd(&tpm_pt->ctrl_dev, CMD_GET_TPMESTABLISHED, &est, 0,
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

        if (tpm_emulator_ctrlcmd(&tpm_pt->ctrl_dev, CMD_RESET_TPMESTABLISHED,
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

    return tpm_pt->had_startup_error;
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
            if (tpm_emulator_ctrlcmd(&tpm_pt->ctrl_dev, CMD_CANCEL_TPM_CMD,
                                     &res, 0, sizeof(res)) < 0) {
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

static TPMVersion tpm_emulator_get_tpm_version(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);

    return tpm_pt->tpm_version;
}

static void tpm_emulator_block_migration(TPMEmulator *tpm_pt)
{
    Error *err = NULL;

    error_setg(&tpm_pt->migration_blocker,
               "Migration disabled: TPM emulator not yet migratable");
    migrate_add_blocker(tpm_pt->migration_blocker, &err);
    if (err) {
        error_free(err);
        error_free(tpm_pt->migration_blocker);
        tpm_pt->migration_blocker = NULL;
    }
}

static int tpm_emulator_prepare_data_fd(TPMEmulator *tpm_pt)
{
    ptm_res res;
    Error *err = NULL;
    int fds[2] = { -1, -1 };

    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) < 0) {
        error_report("tpm-emulator: Failed to create socketpair");
        return -1;
    }

    qemu_chr_fe_set_msgfds(&tpm_pt->ctrl_dev, fds + 1, 1);

    if (tpm_emulator_ctrlcmd(&tpm_pt->ctrl_dev, CMD_SET_DATAFD, &res, 0,
                    sizeof(res)) || res != 0) {
        error_report("tpm-emulator: Failed to send CMD_SET_DATAFD: %s",
                     strerror(errno));
        goto err_exit;
    }

    tpm_pt->data_ioc = QIO_CHANNEL(qio_channel_socket_new_fd(fds[0], &err));
    if (err) {
        error_report("tpm-emulator: Failed to create io channel : %s",
                       error_get_pretty(err));
        error_free(err);
        goto err_exit;
    }

    return 0;

err_exit:
    closesocket(fds[0]);
    closesocket(fds[1]);
    return -1;
}

static int tpm_emulator_handle_device_opts(TPMEmulator *tpm_pt, QemuOpts *opts)
{
    const char *value;

    value = qemu_opt_get(opts, "chardev");
    if (value) {
        Error *err = NULL;
        Chardev *dev = qemu_chr_find(value);

        tpm_pt->options->chardev = g_strdup(value);

        if (!dev || !qemu_chr_fe_init(&tpm_pt->ctrl_dev, dev, &err)) {
            error_report("tpm-emulator: No valid chardev found at '%s': %s",
                         value, err ? error_get_pretty(err) : "");
            error_free(err);
            goto err;
        }
    }

    if (tpm_emulator_prepare_data_fd(tpm_pt) < 0) {
        goto err;
    }

    /* FIXME: tpm_util_test_tpmdev() accepts only on socket fd, as it also used
     * by passthrough driver, which not yet using GIOChannel.
     */
    if (tpm_util_test_tpmdev(QIO_CHANNEL_SOCKET(tpm_pt->data_ioc)->fd,
                             &tpm_pt->tpm_version)) {
        error_report("'%s' is not emulating TPM device. Error: %s",
                      tpm_pt->options->chardev, strerror(errno));
        goto err;
    }

    DPRINTF("TPM Version %s", tpm_pt->tpm_version == TPM_VERSION_1_2 ? "1.2" :
             (tpm_pt->tpm_version == TPM_VERSION_2_0 ?  "2.0" : "Unspecified"));

    if (tpm_emulator_probe_caps(tpm_pt) ||
        tpm_emulator_check_caps(tpm_pt)) {
        goto err;
    }

    tpm_emulator_block_migration(tpm_pt);

    return 0;

err:
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

static TpmTypeOptions *tpm_emulator_get_tpm_options(TPMBackend *tb)
{
    TPMEmulator *tpm_pt = TPM_EMULATOR(tb);
    TpmTypeOptions *options = NULL;
    TPMEmulatorOptions *eoptions = NULL;

    eoptions = g_new0(TPMEmulatorOptions, 1);
    if (!eoptions) {
        return NULL;
    }
    DPRINTF("%s", __func__);

    eoptions->chardev = g_strdup(tpm_pt->options->chardev);
    options = g_new0(TpmTypeOptions, 1);
    if (!options) {
        qapi_free_TPMEmulatorOptions(eoptions);
        return NULL;
    }

    options->type = TPM_TYPE_EMULATOR;
    options->u.emulator.data = eoptions;

    return options;
}

static const QemuOptDesc tpm_emulator_cmdline_opts[] = {
    TPM_STANDARD_CMDLINE_OPTS,
    {
        .name = "chardev",
        .type = QEMU_OPT_STRING,
        .help = "Character device to use for out-of-band control messages",
    },
    { /* end of list */ },
};

static const TPMDriverOps tpm_emulator_driver = {
    .type                     = TPM_TYPE_EMULATOR,
    .opts                     = tpm_emulator_cmdline_opts,
    .desc                     = "TPM emulator backend driver",

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
    tpm_pt->options = g_new0(TPMEmulatorOptions, 1);
    tpm_pt->op_executing = tpm_pt->op_canceled = false;
    tpm_pt->had_startup_error = false;
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

    qemu_chr_fe_deinit(&tpm_pt->ctrl_dev, false);

    if (tpm_pt->options) {
        qapi_free_TPMEmulatorOptions(tpm_pt->options);
    }

    if (tpm_pt->migration_blocker) {
        migrate_del_blocker(tpm_pt->migration_blocker);
        error_free(tpm_pt->migration_blocker);
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
