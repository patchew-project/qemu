/*
 * QEMU Crypto Device Implement
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
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
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qemu/iov.h"
#include "qapi-visit.h"
#include "qapi/opts-visitor.h"

#include "crypto/crypto.h"
#include "qemu/config-file.h"
#include "monitor/monitor.h"
#include "crypto/crypto-clients.h"


static QTAILQ_HEAD(, CryptoClientState) crypto_clients;

QemuOptsList qemu_cryptodev_opts = {
    .name = "cryptodev",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_cryptodev_opts.head),
    .desc = {
        /*
         * no elements => accept any params
         * validation will happen later
         */
        { /* end of list */ }
    },
};

static int
crypto_init_cryptodev(void *dummy, QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;
    int ret;

    ret = crypto_client_init(opts, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return -1;
    }

    return ret;
}

int crypto_init_clients(void)
{
    QTAILQ_INIT(&crypto_clients);

    if (qemu_opts_foreach(qemu_find_opts("cryptodev"),
                          crypto_init_cryptodev, NULL, NULL)) {
        return -1;
    }

    return 0;
}

static int (* const crypto_client_init_fun[CRYPTO_CLIENT_OPTIONS_KIND__MAX])(
    const CryptoClientOptions *opts,
    const char *name,
    CryptoClientState *peer, Error **errp) = {
#ifdef CONFIG_CRYPTODEV_LINUX
        [CRYPTO_CLIENT_OPTIONS_KIND_CRYPTODEV_LINUX] =
                                             crypto_init_cryptodev_linux,
#endif
};

static int crypto_client_init1(const void *object, Error **errp)
{
    const CryptoClientOptions *opts;
    const char *name;

    const Cryptodev *cryptodev = object;
    opts = cryptodev->opts;
    name = cryptodev->id;

    if (opts->type == CRYPTO_CLIENT_OPTIONS_KIND_LEGACY_HW ||
        !crypto_client_init_fun[opts->type]) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "type",
                   "a cryptodev backend type");
        return -1;
    }

    if (crypto_client_init_fun[opts->type](opts, name, NULL, errp) < 0) {
        if (errp && !*errp) {
            error_setg(errp, QERR_DEVICE_INIT_FAILED,
                       CryptoClientOptionsKind_lookup[opts->type]);
        }
        return -1;
    }
    return 0;
}

int crypto_client_init(QemuOpts *opts, Error **errp)
{
    void *object = NULL;
    Error *err = NULL;
    int ret = -1;
    Visitor *v = opts_visitor_new(opts);

    visit_type_Cryptodev(v, NULL, (Cryptodev **)&object, &err);
    if (!err) {
        ret = crypto_client_init1(object, &err);
    }

    qapi_free_Cryptodev(object);
    error_propagate(errp, err);
    return ret;
}

static void crypto_client_destructor(CryptoClientState *cc)
{
    g_free(cc);
}

static void crypto_client_setup(CryptoClientState *cc,
                                  CryptoClientInfo *info,
                                  CryptoClientState *peer,
                                  const char *model,
                                  const char *name,
                                  CryptoClientDestructor *destructor)
{
    cc->info = info;
    cc->model = g_strdup(model);
    if (name) {
        cc->name = g_strdup(name);
    }

    if (peer) {
        assert(!peer->peer);
        cc->peer = peer;
        peer->peer = cc;
    }
    QTAILQ_INSERT_TAIL(&crypto_clients, cc, next);
    cc->destructor = destructor;
    cc->incoming_queue =
                qemu_new_crypto_queue(qemu_deliver_crypto_packet, cc);
}

CryptoClientState *new_crypto_client(CryptoClientInfo *info,
                                    CryptoClientState *peer,
                                    const char *model,
                                    const char *name)
{
    CryptoClientState *cc;

    assert(info->size >= sizeof(CryptoClientState));

    /* allocate the memroy of CryptoClientState's parent */
    cc = g_malloc0(info->size);
    crypto_client_setup(cc, info, peer, model, name,
                          crypto_client_destructor);

    return cc;
}

int qemu_deliver_crypto_packet(CryptoClientState *sender,
                              unsigned flags,
                              void *header_opqaue,
                              void *opaque)
{
    CryptoClientState *cc = opaque;
    int ret = -1;

    if (!cc->ready) {
        return 1;
    }

    if (flags == QEMU_CRYPTO_PACKET_FLAG_SYM) {
        CryptoSymOpInfo *op_info = header_opqaue;
        if (cc->info->do_sym_op) {
            ret = cc->info->do_sym_op(cc, op_info);
        }
    }

    return ret;
}

int qemu_send_crypto_packet_async(CryptoClientState *sender,
                                unsigned flags,
                                void *opaque,
                                CryptoPacketSent *sent_cb)
{
    CryptoQueue *queue;

    if (!sender->ready) {
        /* we assume that all packets are sent */
        return 1;
    }

    queue = sender->peer->incoming_queue;

    return qemu_crypto_queue_send(queue, flags, sender,
                                  opaque, sent_cb);
}

CryptoLegacyHWState *
qemu_new_crypto_legacy_hw(CryptoClientInfo *info,
                           CryptoLegacyHWConf *conf,
                           const char *model,
                           const char *name,
                           void *opaque)
{
    CryptoLegacyHWState *crypto;
    CryptoClientState **peers = conf->peers.ccs;
    int i, queues = MAX(1, conf->peers.queues);

    assert(info->type == CRYPTO_CLIENT_OPTIONS_KIND_LEGACY_HW);
    assert(info->size >= sizeof(CryptoLegacyHWState));

    crypto = g_malloc0(info->size + sizeof(CryptoClientState) * queues);
    crypto->ccs = (void *)crypto + info->size;
    crypto->opaque = opaque;
    crypto->conf = conf;

    for (i = 0; i < queues; i++) {
        crypto_client_setup(&crypto->ccs[i], info, peers[i], model, name,
                              NULL);
        crypto->ccs[i].queue_index = i;
        crypto->ccs[i].ready = true;
    }

    return crypto;
}

static void qemu_cleanup_crypto_client(CryptoClientState *cc)
{
    QTAILQ_REMOVE(&crypto_clients, cc, next);

    if (cc->info->cleanup) {
        cc->info->cleanup(cc);
    }
}

static void qemu_free_crypto_client(CryptoClientState *cc)
{
    if (cc->incoming_queue) {
        qemu_del_crypto_queue(cc->incoming_queue);
    }
    if (cc->peer) {
        cc->peer->peer = NULL;
    }
    g_free(cc->model);
    g_free(cc->name);

    if (cc->destructor) {
        cc->destructor(cc);
    }
}

CryptoClientState *
qemu_get_crypto_subqueue(CryptoLegacyHWState *crypto, int queue_index)
{
    return crypto->ccs + queue_index;
}

void qemu_del_crypto_legacy_hw(CryptoLegacyHWState *crypto)
{
    int i, queues = MAX(crypto->conf->peers.queues, 1);

    for (i = queues - 1; i >= 0; i--) {
        CryptoClientState *cc = qemu_get_crypto_subqueue(crypto, i);

        qemu_cleanup_crypto_client(cc);
        qemu_free_crypto_client(cc);
    }

    g_free(crypto);
}

CryptoLegacyHWState *qemu_get_crypto_legacy_hw(CryptoClientState *cc)
{
    CryptoClientState *cc0 = cc - cc->queue_index;

    return (CryptoLegacyHWState *)((void *)cc0 - cc->info->size);
}

void *qemu_get_crypto_legacy_hw_opaque(CryptoClientState *cc)
{
    CryptoLegacyHWState *crypto = qemu_get_crypto_legacy_hw(cc);

    return crypto->opaque;
}

int qemu_find_crypto_clients_except(const char *id, CryptoClientState **ccs,
                                 CryptoClientOptionsKind type, int max)
{
    CryptoClientState *cc;
    int ret = 0;

    QTAILQ_FOREACH(cc, &crypto_clients, next) {
        if (cc->info->type == type) {
            continue;
        }
        if (!id || !strcmp(cc->name, id)) {
            if (ret < max) {
                ccs[ret] = cc;
            }
            ret++;
        }
    }

    return ret;
}

int qemu_crypto_create_session(CryptoClientState *cc,
                               CryptoSymSessionInfo *info,
                               uint64_t *session_id)
{
    int ret = -1;
    CryptoClientState *peer = cc->peer;

    if (!peer || !peer->ready) {
        return ret;
    }

    if (peer->info->create_session) {
        ret = peer->info->create_session(peer, info, session_id);
    }
    return ret;
}

int qemu_crypto_close_session(CryptoClientState *cc,
                               uint64_t session_id)
{
    int ret = -1;
    CryptoClientState *peer = cc->peer;

    if (!peer || !peer->ready) {
        return ret;
    }

    if (peer->info->close_session) {
        ret = peer->info->close_session(peer, session_id);
    }
    return ret;
}
