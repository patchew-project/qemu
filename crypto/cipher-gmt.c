/*
 * QEMU GM/T 0018-2012 cryptographic standard support
 *
 * Copyright (c) 2024 SmartX Inc
 *
 * Authors:
 *    Hyman Huang <yong.huang@smartx.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */
#include <gmt-0018-2012.h>

#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qapi/error.h"
#include "crypto/cipher.h"
#include "cipherpriv.h"

#include "qemu/error-report.h"

typedef struct QCryptoGMT QCryptoGMT;

struct QCryptoGMT {
    QCryptoCipher base;

    SGD_HANDLE session;
    SGD_HANDLE key;
    SGD_UINT32 alg;
    unsigned char iv[16];  /* not used for SM4 algo currently */
};

typedef struct QCryptoGMTDeviceInfo QCryptoGMTDeviceInfo;

struct QCryptoGMTDeviceInfo {
    SGD_HANDLE device;
    struct DeviceInfo_st info;
    bool opened;
    gint ref_count;
};
/*
 * It is advised to use numerous sessions with one open device
 * as opposed to single sessions with several devices.
 */
static QCryptoGMTDeviceInfo gmt_device;
/* Protect the gmt_device */
static QemuMutex gmt_device_mutex;

static const struct QCryptoCipherDriver qcrypto_cipher_gmt_driver;

static void gmt_device_lock(void)
{
    qemu_mutex_lock(&gmt_device_mutex);
}

static void gmt_device_unlock(void)
{
    qemu_mutex_unlock(&gmt_device_mutex);
}

static void
__attribute__((__constructor__)) gmt_device_mutex_init(void)
{
    qemu_mutex_init(&gmt_device_mutex);
}

static void
gmt_device_ref(void)
{
    g_assert(gmt_device.device != NULL);
    g_atomic_int_inc(&gmt_device.ref_count);
}

static void
gmt_device_unref(void)
{
    g_assert(gmt_device.device != NULL);
    if (g_atomic_int_dec_and_test(&gmt_device.ref_count)) {
        SDF_CloseDevice(gmt_device.device);
        gmt_device.opened = false;
        gmt_device.device = NULL;
        memset(&gmt_device.info, 0, sizeof(struct DeviceInfo_st));
    }
}

static bool
qcrypto_gmt_cipher_supports(QCryptoCipherAlgorithm alg,
                            QCryptoCipherMode mode)
{
    switch (alg) {
    case QCRYPTO_CIPHER_ALG_SM4:
        break;
    default:
        return false;
    }

    switch (mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
        return true;
    default:
        return false;
    }
}

QCryptoCipher *
qcrypto_gmt_cipher_ctx_new(QCryptoCipherAlgorithm alg,
                           QCryptoCipherMode mode,
                           const uint8_t *key,
                           size_t nkey,
                           Error **errp)
{
    QCryptoGMT *gmt;
    int rv;

    if (!qcrypto_gmt_cipher_supports(alg, mode)) {
        return NULL;
    }

    gmt = g_new0(QCryptoGMT, 1);
    if (!gmt) {
        return NULL;
    }

    switch (alg) {
    case QCRYPTO_CIPHER_ALG_SM4:
        gmt->alg = SGD_SM4_ECB;
        break;
    default:
        return NULL;
    }

    gmt_device_lock();
    if (!gmt_device.opened) {
        rv = SDF_OpenDevice(&gmt_device.device);
        if (rv != SDR_OK) {
            info_report("Could not open encryption card device, disabling");
            goto abort;
        }
        gmt_device.opened = true;
    }

    /*
     * multi-sessions correspond to an opened device handle
     */
    rv = SDF_OpenSession(gmt_device.device, &gmt->session);
    if (rv != SDR_OK) {
        error_setg(errp, "Open session failed");
        goto abort;
    }

    gmt_device_ref();

    if (!(gmt_device.info.SymAlgAbility)) {
        rv = SDF_GetDeviceInfo(gmt->session, &gmt_device.info);
        if (rv != SDR_OK) {
            error_setg(errp, "Get device info failed");
            goto abort;
        }
    }
    gmt_device_unlock();

    if (!(gmt_device.info.SymAlgAbility & SGD_SM4_ECB & SGD_SYMM_ALG_MASK)) {
        /* encryption card do not support SM4 cipher algorithm */
        info_report("SM4 cipher algorithm is not supported, disabling");
        return NULL;
    }

    rv = SDF_ImportKey(gmt->session, (SGD_UCHAR *)key,
                       (SGD_UINT32)nkey, &gmt->key);
    if (rv != SDR_OK) {
        error_setg(errp, "Import key failed");
        return NULL;
    }

    gmt->base.alg = alg;
    gmt->base.mode = mode;
    gmt->base.driver = &qcrypto_cipher_gmt_driver;
    return &gmt->base;

abort:
    gmt_device_unlock();
    return NULL;
}

static int
qcrypto_gmt_cipher_setiv(QCryptoCipher *cipher,
                         const uint8_t *iv,
                         size_t niv, Error **errp)
{
    error_setg(errp, "Setting IV is not supported");
    return -1;
}

static int
qcrypto_gmt_cipher_op(QCryptoGMT *gmt,
                      const void *in, void *out,
                      size_t len, bool do_encrypt,
                      Error **errp)
{
    unsigned int rlen;
    int rv;

    if (do_encrypt) {
        rv = SDF_Encrypt(gmt->session, gmt->key, gmt->alg, gmt->iv,
                         (SGD_UCHAR *)in, len, out, &rlen);
    } else {
        rv = SDF_Decrypt(gmt->session, gmt->key, gmt->alg, gmt->iv,
                         (SGD_UCHAR *)in, len, out, &rlen);
    }

    if (rv != SDR_OK) {
        error_setg(errp, "Crypto operation failed");
        return -1;
    }

    return 0;
}

static void
qcrypto_gmt_free(QCryptoGMT *gmt)
{
    g_assert(gmt != NULL);

    SDF_DestroyKey(gmt->session, gmt->key);
    SDF_CloseSession(gmt->session);

    gmt_device_lock();
    gmt_device_unref();
    gmt_device_unlock();
}

static int
qcrypto_gmt_cipher_encrypt(QCryptoCipher *cipher,
                           const void *in, void *out,
                           size_t len, Error **errp)
{
    QCryptoGMT *gmt = container_of(cipher, QCryptoGMT, base);
    return qcrypto_gmt_cipher_op(gmt, in, out, len, true, errp);
}

static int
qcrypto_gmt_cipher_decrypt(QCryptoCipher *cipher,
                           const void *in, void *out,
                           size_t len, Error **errp)
{
    QCryptoGMT *gmt = container_of(cipher, QCryptoGMT, base);
    return qcrypto_gmt_cipher_op(gmt, in, out, len, false, errp);
}

static void qcrypto_gmt_comm_ctx_free(QCryptoCipher *cipher)
{
    QCryptoGMT *gmt = container_of(cipher, QCryptoGMT, base);
    qcrypto_gmt_free(gmt);
    g_free(gmt);
}

static const struct QCryptoCipherDriver qcrypto_cipher_gmt_driver = {
    .cipher_encrypt = qcrypto_gmt_cipher_encrypt,
    .cipher_decrypt = qcrypto_gmt_cipher_decrypt,
    .cipher_setiv = qcrypto_gmt_cipher_setiv,
    .cipher_free = qcrypto_gmt_comm_ctx_free,
};
