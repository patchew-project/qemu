/*
 * Emulator TPM driver which connects over the mssim protocol
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2022
 * Author: James Bottomley <jejb@linux.ibm.com>
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"

#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-tpm.h"

#include "io/channel-socket.h"

#include "sysemu/tpm_backend.h"
#include "sysemu/tpm_util.h"

#include "qom/object.h"

#include "tpm_int.h"
#include "tpm_mssim.h"

#define ERROR_PREFIX "TPM mssim Emulator: "

#define TYPE_TPM_MSSIM "tpm-mssim"
OBJECT_DECLARE_SIMPLE_TYPE(TPMmssim, TPM_MSSIM)

struct TPMmssim {
    TPMBackend parent;

    TPMmssimOptions opts;

    QIOChannel *cmd_qc, *ctrl_qc;
};

static int tpm_send_ctrl(TPMmssim *t, uint32_t cmd, Error **errp)
{
    int ret;

    cmd = htonl(cmd);
    ret = qio_channel_write_all(t->ctrl_qc, (char *)&cmd, sizeof(cmd), errp);
    if (ret != 0)
        return ret;
    ret = qio_channel_read_all(t->ctrl_qc, (char *)&cmd, sizeof(cmd), errp);
    if (ret != 0)
        return ret;
    if (cmd != 0) {
        error_setg(errp, ERROR_PREFIX "Incorrect ACK recieved on control channel 0x%x\n", cmd);
        return -1;
    }
    return 0;
}

static void tpm_mssim_instance_init(Object *obj)
{
}

static void tpm_mssim_instance_finalize(Object *obj)
{
    TPMmssim *t = TPM_MSSIM(obj);

    tpm_send_ctrl(t, TPM_SIGNAL_POWER_OFF, NULL);

    object_unref(OBJECT(t->ctrl_qc));
    object_unref(OBJECT(t->cmd_qc));
}

static void tpm_mssim_cancel_cmd(TPMBackend *tb)
{
        return;
}

static TPMVersion tpm_mssim_get_version(TPMBackend *tb)
{
    return TPM_VERSION_2_0;
}

static size_t tpm_mssim_get_buffer_size(TPMBackend *tb)
{
    /* TCG standard profile max buffer size */
    return 4096;
}

static TpmTypeOptions *tpm_mssim_get_opts(TPMBackend *tb)
{
    TPMmssim *t = TPM_MSSIM(tb);
    TpmTypeOptions *opts = g_new0(TpmTypeOptions, 1);

    opts->type = TPM_TYPE_MSSIM;
    opts->u.mssim.data = QAPI_CLONE(TPMmssimOptions, &t->opts);

    return opts;
}

static void tpm_mssim_handle_request(TPMBackend *tb, TPMBackendCmd *cmd,
                                     Error **errp)
{
    TPMmssim *t = TPM_MSSIM(tb);
    uint32_t header, len;
    uint8_t locality = cmd->locty;
    struct iovec iov[4];
    int ret;

    header = htonl(TPM_SEND_COMMAND);
    len = htonl(cmd->in_len);

    iov[0].iov_base = &header;
    iov[0].iov_len = sizeof(header);
    iov[1].iov_base = &locality;
    iov[1].iov_len = sizeof(locality);
    iov[2].iov_base = &len;
    iov[2].iov_len = sizeof(len);
    iov[3].iov_base = (void *)cmd->in;
    iov[3].iov_len = cmd->in_len;

    ret = qio_channel_writev_all(t->cmd_qc, iov, 4, errp);
    if (ret != 0)
        goto fail;

    ret = qio_channel_read_all(t->cmd_qc, (char *)&len, sizeof(len), errp);
    if (ret != 0)
        goto fail;
    len = ntohl(len);
    if (len > cmd->out_len) {
        error_setg(errp, "receive size is too large");
        goto fail;
    }
    ret = qio_channel_read_all(t->cmd_qc, (char *)cmd->out, len, errp);
    if (ret != 0)
        goto fail;
    /* ACK packet */
    ret = qio_channel_read_all(t->cmd_qc, (char *)&header, sizeof(header), errp);
    if (ret != 0)
        goto fail;
    if (header != 0) {
        error_setg(errp, "incorrect ACK received on command channel 0x%x", len);
        goto fail;
    }

    return;

 fail:
    error_prepend(errp, ERROR_PREFIX);
    tpm_util_write_fatal_error_response(cmd->out, cmd->out_len);
}

static TPMBackend *tpm_mssim_create(QemuOpts *opts)
{
    TPMBackend *be = TPM_BACKEND(object_new(TYPE_TPM_MSSIM));
    TPMmssim *t = TPM_MSSIM(be);
    InetSocketAddress cmd_s, ctl_s;
    int sock;
    const char *host, *port, *ctrl;
    Error *errp = NULL;

    host = qemu_opt_get(opts, "host");
    if (!host)
        host = "localhost";
    t->opts.host = g_strdup(host);

    port = qemu_opt_get(opts, "port");
    if (!port)
        port = "2321";
    t->opts.port = g_strdup(port);

    ctrl = qemu_opt_get(opts, "ctrl");
    if (!ctrl)
        ctrl = "2322";
    t->opts.ctrl = g_strdup(ctrl);

    cmd_s.host = (char *)host;
    cmd_s.port = (char *)port;

    ctl_s.host = (char *)host;
    ctl_s.port = (char *)ctrl;

    sock = inet_connect_saddr(&cmd_s, &errp);
    if (sock < 0)
        goto fail;
    t->cmd_qc = QIO_CHANNEL(qio_channel_socket_new_fd(sock, &errp));
    if (errp)
        goto fail;
    sock = inet_connect_saddr(&ctl_s, &errp);
    if (sock < 0)
        goto fail_unref_cmd;
    t->ctrl_qc = QIO_CHANNEL(qio_channel_socket_new_fd(sock, &errp));
    if (errp)
        goto fail_unref_cmd;

    /* reset the TPM using a power cycle sequence, in case someone
     * has previously powered it up */
    sock = tpm_send_ctrl(t, TPM_SIGNAL_POWER_OFF, &errp);
    if (sock != 0)
        goto fail_unref;
    sock = tpm_send_ctrl(t, TPM_SIGNAL_POWER_ON, &errp);
    if (sock != 0)
        goto fail_unref;
    sock = tpm_send_ctrl(t, TPM_SIGNAL_NV_ON, &errp);
    if (sock != 0)
        goto fail_unref;

    return be;
 fail_unref:
    object_unref(OBJECT(t->ctrl_qc));
 fail_unref_cmd:
    object_unref(OBJECT(t->cmd_qc));
 fail:
    error_prepend(&errp, ERROR_PREFIX);
    error_report_err(errp);
    object_unref(OBJECT(be));

    return NULL;
}

static const QemuOptDesc tpm_mssim_cmdline_opts[] = {
    TPM_STANDARD_CMDLINE_OPTS,
    {
        .name = "host",
        .type = QEMU_OPT_STRING,
        .help = "name or IP address of host to connect to (deault localhost)",
    },
    {
        .name = "port",
        .type = QEMU_OPT_STRING,
        .help = "port number for standard TPM commands (default 2321)",
    },
    {
        .name = "ctrl",
        .type = QEMU_OPT_STRING,
        .help = "control port for TPM commands (default 2322)",
    },
};

static void tpm_mssim_class_init(ObjectClass *klass, void *data)
{
    TPMBackendClass *cl = TPM_BACKEND_CLASS(klass);

    cl->type = TPM_TYPE_MSSIM;
    cl->opts = tpm_mssim_cmdline_opts;
    cl->desc = "TPM mssim emulator backend driver";
    cl->create = tpm_mssim_create;
    cl->cancel_cmd = tpm_mssim_cancel_cmd;
    cl->get_tpm_version = tpm_mssim_get_version;
    cl->get_buffer_size = tpm_mssim_get_buffer_size;
    cl->get_tpm_options = tpm_mssim_get_opts;
    cl->handle_request = tpm_mssim_handle_request;
}

static const TypeInfo tpm_mssim_info = {
    .name = TYPE_TPM_MSSIM,
    .parent = TYPE_TPM_BACKEND,
    .instance_size = sizeof(TPMmssim),
    .class_init = tpm_mssim_class_init,
    .instance_init = tpm_mssim_instance_init,
    .instance_finalize = tpm_mssim_instance_finalize,
};

static void tpm_mssim_register(void)
{
    type_register_static(&tpm_mssim_info);
}

type_init(tpm_mssim_register)
