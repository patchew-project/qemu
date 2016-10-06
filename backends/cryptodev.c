/*
 * QEMU Crypto Device Implementation
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "sysemu/cryptodev.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi-types.h"
#include "qapi-visit.h"
#include "qemu/config-file.h"
#include "qom/object_interfaces.h"

static QTAILQ_HEAD(, QCryptoCryptoDevBackendClientState) crypto_clients;


QCryptoCryptoDevBackendClientState *
qcrypto_cryptodev_backend_new_client(const char *model,
                                    const char *name)
{
    QCryptoCryptoDevBackendClientState *cc;

    cc = g_malloc0(sizeof(QCryptoCryptoDevBackendClientState));
    cc->model = g_strdup(model);
    if (name) {
        cc->name = g_strdup(name);
    }

    QTAILQ_INSERT_TAIL(&crypto_clients, cc, next);

    return cc;
}

void qcrypto_cryptodev_backend_free_client(
                  QCryptoCryptoDevBackendClientState *cc)
{
    QTAILQ_REMOVE(&crypto_clients, cc, next);
    g_free(cc->name);
    g_free(cc->model);
    g_free(cc->info_str);
    g_free(cc);
}

void qcrypto_cryptodev_backend_cleanup(
             QCryptoCryptoDevBackend *backend,
             Error **errp)
{
    QCryptoCryptoDevBackendClass *bc =
                  QCRYPTO_CRYPTODEV_BACKEND_GET_CLASS(backend);

    if (bc->cleanup) {
        bc->cleanup(backend, errp);
    }

    backend->ready = false;
}

static void
qcrypto_cryptodev_backend_get_queues(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    QCryptoCryptoDevBackend *backend = QCRYPTO_CRYPTODEV_BACKEND(obj);
    uint32_t value = backend->conf.peers.queues;

    visit_type_uint32(v, name, &value, errp);
}

static void
qcrypto_cryptodev_backend_set_queues(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    QCryptoCryptoDevBackend *backend = QCRYPTO_CRYPTODEV_BACKEND(obj);
    Error *local_err = NULL;
    uint32_t value;

    visit_type_uint32(v, name, &value, &local_err);
    if (local_err) {
        goto out;
    }
    if (!value) {
        error_setg(&local_err, "Property '%s.%s' doesn't take value '%"
                   PRIu32 "'", object_get_typename(obj), name, value);
        goto out;
    }
    backend->conf.peers.queues = value;
out:
    error_propagate(errp, local_err);
}

static void
qcrypto_cryptodev_backend_complete(UserCreatable *uc, Error **errp)
{
    QCryptoCryptoDevBackend *backend = QCRYPTO_CRYPTODEV_BACKEND(uc);
    QCryptoCryptoDevBackendClass *bc = QCRYPTO_CRYPTODEV_BACKEND_GET_CLASS(uc);
    Error *local_err = NULL;

    if (bc->init) {
        bc->init(backend, &local_err);
        if (local_err) {
            goto out;
        }
    }
    backend->ready = true;
    return;

out:
    backend->ready = false;
    error_propagate(errp, local_err);
}

static void qcrypto_cryptodev_backend_instance_init(Object *obj)
{
    object_property_add(obj, "queues", "int",
                          qcrypto_cryptodev_backend_get_queues,
                          qcrypto_cryptodev_backend_set_queues,
                          NULL, NULL, NULL);
    /* Initialize devices' queues property to 1 */
    object_property_set_int(obj, 1, "queues", NULL);
}

static void qcrypto_cryptodev_backend_finalize(Object *obj)
{

}

static void
qcrypto_cryptodev_backend_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = qcrypto_cryptodev_backend_complete;

    QTAILQ_INIT(&crypto_clients);
}

static const TypeInfo qcrypto_cryptodev_backend_info = {
    .name = TYPE_QCRYPTO_CRYPTODEV_BACKEND,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(QCryptoCryptoDevBackend),
    .instance_init = qcrypto_cryptodev_backend_instance_init,
    .instance_finalize = qcrypto_cryptodev_backend_finalize,
    .class_size = sizeof(QCryptoCryptoDevBackendClass),
    .class_init = qcrypto_cryptodev_backend_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void
qcrypto_cryptodev_backend_register_types(void)
{
    type_register_static(&qcrypto_cryptodev_backend_info);
}

type_init(qcrypto_cryptodev_backend_register_types);
