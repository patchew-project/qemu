/*
 * Xen Stubdom vTPM driver
 *
 *  Copyright (c) 2015 Intel Corporation
 *  Authors:
 *    Quan Xu <quan.xu@intel.com>
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
 */

#include "qemu/osdep.h"
#include "sysemu/tpm_backend.h"
#include "tpm_int.h"
#include "tpm_util.h"
#include "hw/xen/xen_pvdev.h"
#include "sysemu/tpm_backend_int.h"
#include "hw/tpm/xen_vtpm_frontend.h"

#define DEBUG_TPM 0
#define DPRINTF(fmt, ...) do { \
    if (DEBUG_TPM) { \
        fprintf(stderr, fmt, ## __VA_ARGS__); \
    } \
} while (0)

#define TYPE_TPM_XENSTUBDOMS "tpm-xenstubdoms"
#define TPM_XENSTUBDOMS(obj) \
    OBJECT_CHECK(TPMXenstubdomsState, (obj), TYPE_TPM_XENSTUBDOMS)

static const TPMDriverOps tpm_xenstubdoms_driver;

/* Data structures */
typedef struct TPMXenstubdomsThreadParams {
    TPMState *tpm_state;
    TPMRecvDataCB *recv_data_callback;
    TPMBackend *tb;
} TPMXenstubdomsThreadParams;

struct TPMXenstubdomsState {
    TPMBackend parent;
    TPMBackendThread tbt;
    TPMXenstubdomsThreadParams tpm_thread_params;
    bool had_startup_error;
};

typedef struct TPMXenstubdomsState TPMXenstubdomsState;

/* Functions */
static void tpm_xenstubdoms_cancel_cmd(TPMBackend *tb);

static int tpm_xenstubdoms_unix_transfer(const TPMLocality *locty_data,
                                         bool *selftest_done)
{
    size_t rlen;
    struct XenDevice *xendev;
    int ret;
    bool is_selftest;
    const struct tpm_resp_hdr *hdr;

    is_selftest = tpm_util_is_selftest(locty_data->w_buffer.buffer,
                                       locty_data->w_buffer.size);

    xendev = xen_pv_find_xendev("vtpm", xen_domid, xenstore_vtpm_dev);
    if (xendev == NULL) {
        xen_pv_printf(xendev, 0, "Can not find vtpm device.\n");
        return -1;
    }

    ret = vtpm_send(xendev, locty_data->w_buffer.buffer,
                    locty_data->w_offset);
    if (ret < 0) {
        xen_pv_printf(xendev, 0, "Can not send vtpm command.\n");
        goto err_exit;
    }

    ret = vtpm_recv(xendev, locty_data->r_buffer.buffer, &rlen);
    if (ret < 0) {
        xen_pv_printf(xendev, 0, "vtpm reception command error.\n");
        goto err_exit;
    }

    if (is_selftest && (ret >= sizeof(struct tpm_resp_hdr))) {
        hdr = (struct tpm_resp_hdr *)locty_data->r_buffer.buffer;
        *selftest_done = (be32_to_cpu(hdr->errcode) == 0);
    }

err_exit:
    if (ret < 0) {
        xen_pv_printf(xendev, 0, "vtpm command error.\n");
    }
    return ret;
}

static void tpm_xenstubdoms_worker_thread(gpointer data,
                                          gpointer user_data)
{
    TPMXenstubdomsThreadParams *thr_parms = user_data;
    TPMBackendCmd cmd = (TPMBackendCmd)data;
    bool selftest_done = false;

    switch (cmd) {
    case TPM_BACKEND_CMD_PROCESS_CMD:
        tpm_xenstubdoms_unix_transfer(thr_parms->tpm_state->locty_data,
                                      &selftest_done);
        thr_parms->recv_data_callback(thr_parms->tpm_state,
                                      thr_parms->tpm_state->locty_number,
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
 *Start the TPM (thread). If it had been started before, then terminate and
 *start it again.
 */
static int tpm_xenstubdoms_startup_tpm(TPMBackend *tb)
{
    TPMXenstubdomsState *tpm_xs = TPM_XENSTUBDOMS(tb);

    tpm_backend_thread_tpm_reset(&tpm_xs->tbt, tpm_xenstubdoms_worker_thread,
                                 &tpm_xs->tpm_thread_params);

    return 0;
}

static void tpm_xenstubdoms_reset(TPMBackend *tb)
{
    TPMXenstubdomsState *tpm_xs = TPM_XENSTUBDOMS(tb);

    tpm_backend_thread_end(&tpm_xs->tbt);
    tpm_xs->had_startup_error = false;
}

static int tpm_xenstubdoms_init(TPMBackend *tb, TPMState *s,
                                TPMRecvDataCB *recv_data_cb)
{
    TPMXenstubdomsState *tpm_xs = TPM_XENSTUBDOMS(tb);

    tpm_xs->tpm_thread_params.tpm_state = s;
    tpm_xs->tpm_thread_params.recv_data_callback = recv_data_cb;
    tpm_xs->tpm_thread_params.tb = tb;
    return 0;
}

static bool tpm_xenstubdoms_get_tpm_established_flag(TPMBackend *tb)
{
    return false;
}

static bool tpm_xenstubdoms_get_startup_error(TPMBackend *tb)
{
    TPMXenstubdomsState *tpm_xs = TPM_XENSTUBDOMS(tb);

    return tpm_xs->had_startup_error;
}

static size_t tpm_xenstubdoms_realloc_buffer(TPMSizedBuffer *sb)
{
    size_t wanted_size = 4096; /* Linux tpm.c buffer size */

    if (sb->size != wanted_size) {
        sb->buffer = g_realloc(sb->buffer, wanted_size);
        sb->size = wanted_size;
    }
    return sb->size;
}

static void tpm_xenstubdoms_deliver_request(TPMBackend *tb)
{
    TPMXenstubdomsState *tpm_xs = TPM_XENSTUBDOMS(tb);

    tpm_backend_thread_deliver_request(&tpm_xs->tbt);
}

static void tpm_xenstubdoms_cancel_cmd(TPMBackend *tb)
{
}

static const char *tpm_xenstubdoms_create_desc(void)
{
    return "Xenstubdoms TPM backend driver";
}

static TPMBackend *tpm_xenstubdoms_create(QemuOpts *opts, const char *id)
{
    Object *obj = object_new(TYPE_TPM_XENSTUBDOMS);
    TPMBackend *tb = TPM_BACKEND(obj);

    tb->id = g_strdup(id);
    tb->fe_model = -1;
    tb->ops = &tpm_xenstubdoms_driver;
    return tb;
}

static void tpm_xenstubdoms_destroy(TPMBackend *tb)
{
    TPMXenstubdomsState *tpm_xh = TPM_XENSTUBDOMS(tb);

    tpm_backend_thread_end(&tpm_xh->tbt);
    g_free(tb->id);
}

static int tpm_xenstubdoms_reset_tpm_established_flag(TPMBackend *tb,
                                                      uint8_t locty)
{
    /* only a TPM 2.0 will support this */
    return 0;
}
static TPMVersion tpm_xenstubdoms_get_tpm_version(TPMBackend *tb)
{
    return TPM_VERSION_1_2;
}

static const QemuOptDesc tpm_xenstubdoms_cmdline_opts[] = {
    TPM_STANDARD_CMDLINE_OPTS,
    {},
};

static const TPMDriverOps tpm_xenstubdoms_driver = {
    .type                     = TPM_TYPE_XENSTUBDOMS,
    .opts                     = tpm_xenstubdoms_cmdline_opts,
    .desc                     = tpm_xenstubdoms_create_desc,
    .create                   = tpm_xenstubdoms_create,
    .destroy                  = tpm_xenstubdoms_destroy,
    .init                     = tpm_xenstubdoms_init,
    .startup_tpm              = tpm_xenstubdoms_startup_tpm,
    .realloc_buffer           = tpm_xenstubdoms_realloc_buffer,
    .reset                    = tpm_xenstubdoms_reset,
    .had_startup_error        = tpm_xenstubdoms_get_startup_error,
    .deliver_request          = tpm_xenstubdoms_deliver_request,
    .cancel_cmd               = tpm_xenstubdoms_cancel_cmd,
    .get_tpm_established_flag = tpm_xenstubdoms_get_tpm_established_flag,
    .reset_tpm_established_flag = tpm_xenstubdoms_reset_tpm_established_flag,
    .get_tpm_version          = tpm_xenstubdoms_get_tpm_version
};

static void tpm_xenstubdoms_inst_init(Object *obj)
{
}

static void tpm_xenstubdoms_inst_finalize(Object *obj)
{
}

static void tpm_xenstubdoms_class_init(ObjectClass *klass, void *data)
{
    TPMBackendClass *tbc = TPM_BACKEND_CLASS(klass);

    tbc->ops = &tpm_xenstubdoms_driver;
}

static const TypeInfo tpm_xenstubdoms_info = {
    .name = TYPE_TPM_XENSTUBDOMS,
    .parent = TYPE_TPM_BACKEND,
    .instance_size = sizeof(TPMXenstubdomsState),
    .class_init = tpm_xenstubdoms_class_init,
    .instance_init = tpm_xenstubdoms_inst_init,
    .instance_finalize = tpm_xenstubdoms_inst_finalize,
};

static void tpm_xenstubdoms_register(void)
{
    type_register_static(&tpm_xenstubdoms_info);
    tpm_register_driver(&tpm_xenstubdoms_driver);
}

type_init(tpm_xenstubdoms_register)
