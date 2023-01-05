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

#include "sysemu/runstate.h"
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

    QIOChannelSocket *cmd_qc, *ctrl_qc;
};

static int tpm_send_ctrl(TPMmssim *t, uint32_t cmd, Error **errp)
{
    int ret;

    qio_channel_socket_connect_sync(t->ctrl_qc, t->opts.control, errp);
    cmd = htonl(cmd);
    ret = qio_channel_write_all(QIO_CHANNEL(t->ctrl_qc), (char *)&cmd, sizeof(cmd), errp);
    if (ret != 0)
        goto out;
    ret = qio_channel_read_all(QIO_CHANNEL(t->ctrl_qc), (char *)&cmd, sizeof(cmd), errp);
    if (ret != 0)
        goto out;
    if (cmd != 0) {
        error_setg(errp, ERROR_PREFIX "Incorrect ACK recieved on control channel 0x%x\n", cmd);
        ret = -1;
    }
 out:
    qio_channel_close(QIO_CHANNEL(t->ctrl_qc), errp);
    return ret;
}

static void tpm_mssim_instance_init(Object *obj)
{
}

static void tpm_mssim_instance_finalize(Object *obj)
{
    TPMmssim *t = TPM_MSSIM(obj);

    if (t->ctrl_qc && !runstate_check(RUN_STATE_INMIGRATE))
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
    opts->u.mssim = t->opts;

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

    ret = qio_channel_writev_all(QIO_CHANNEL(t->cmd_qc), iov, 4, errp);
    if (ret != 0)
        goto fail;

    ret = qio_channel_read_all(QIO_CHANNEL(t->cmd_qc), (char *)&len, sizeof(len), errp);
    if (ret != 0)
        goto fail;
    len = ntohl(len);
    if (len > cmd->out_len) {
        error_setg(errp, "receive size is too large");
        goto fail;
    }
    ret = qio_channel_read_all(QIO_CHANNEL(t->cmd_qc), (char *)cmd->out, len, errp);
    if (ret != 0)
        goto fail;
    /* ACK packet */
    ret = qio_channel_read_all(QIO_CHANNEL(t->cmd_qc), (char *)&header, sizeof(header), errp);
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

static TPMBackend *tpm_mssim_create(TpmCreateOptions *opts)
{
    TPMBackend *be = TPM_BACKEND(object_new(TYPE_TPM_MSSIM));
    TPMmssim *t = TPM_MSSIM(be);
    int sock;
    Error *errp = NULL;
    TPMmssimOptions *mo = &opts->u.mssim;

    if (!mo->command) {
            mo->command = g_new0(SocketAddress, 1);
            mo->command->type = SOCKET_ADDRESS_TYPE_INET;
            mo->command->u.inet.host = g_strdup("localhost");
            mo->command->u.inet.port = g_strdup("2321");
    }
    if (!mo->control) {
            int port;

            mo->control = g_new0(SocketAddress, 1);
            mo->control->type = SOCKET_ADDRESS_TYPE_INET;
            mo->control->u.inet.host = g_strdup(mo->command->u.inet.host);
            /* in the reference implementation, the control port is
             * always one above the command port */
            port = atoi(mo->command->u.inet.port) + 1;
            mo->control->u.inet.port = g_strdup_printf("%d", port);
    }

    t->opts = opts->u.mssim;
    t->cmd_qc = qio_channel_socket_new();
    t->ctrl_qc = qio_channel_socket_new();

    if (qio_channel_socket_connect_sync(t->cmd_qc, mo->command, &errp) < 0)
        goto fail;

    if (qio_channel_socket_connect_sync(t->ctrl_qc, mo->control, &errp) < 0)
        goto fail;
    qio_channel_close(QIO_CHANNEL(t->ctrl_qc), &errp);

    if (!runstate_check(RUN_STATE_INMIGRATE)) {
        /* reset the TPM using a power cycle sequence, in case someone
         * has previously powered it up */
        sock = tpm_send_ctrl(t, TPM_SIGNAL_POWER_OFF, &errp);
        if (sock != 0)
            goto fail;
        sock = tpm_send_ctrl(t, TPM_SIGNAL_POWER_ON, &errp);
        if (sock != 0)
            goto fail;
        sock = tpm_send_ctrl(t, TPM_SIGNAL_NV_ON, &errp);
        if (sock != 0)
            goto fail;
    }

    return be;

 fail:
    object_unref(OBJECT(t->ctrl_qc));
    object_unref(OBJECT(t->cmd_qc));
    t->ctrl_qc = NULL;
    t->cmd_qc = NULL;
    error_prepend(&errp, ERROR_PREFIX);
    error_report_err(errp);
    object_unref(OBJECT(be));

    return NULL;
}

static const QemuOptDesc tpm_mssim_cmdline_opts[] = {
    TPM_STANDARD_CMDLINE_OPTS,
    {
        .name = "command",
        .type = QEMU_OPT_STRING,
        .help = "Command socket (default localhost:2321)",
    },
    {
        .name = "control",
        .type = QEMU_OPT_STRING,
        .help = "control socket (default localhost:2322)",
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
