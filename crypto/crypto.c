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
    CryptoClientState *peer, Error **errp);

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
