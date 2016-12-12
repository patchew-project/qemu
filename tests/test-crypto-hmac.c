/*
 * QEMU Crypto hmac algorithms tests
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Longpeng(Mike) <longpeng2@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "crypto/init.h"
#include "crypto/hmac.h"

typedef struct QCryptoHmacTestData QCryptoHmacTestData;
struct QCryptoHmacTestData {
    const char *path;
    QCryptoHmacAlgorithm alg;
    const char *key;
    const char *message;
    const char *digest;
};

static QCryptoHmacTestData test_data[] = {
    {
        .path = "/crypto/hmac/hmac-md5",
        .alg = QCRYPTO_HMAC_ALG_MD5,
        .key =
            "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
        .message =
            "4869205468657265",
        .digest =
            "9294727a3638bb1c13f48ef8158bfc9d",
    },
    {
        .path = "/crypto/hmac/hmac-sha1",
        .alg = QCRYPTO_HMAC_ALG_SHA1,
        .key =
            "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"
            "0b0b0b0b",
        .message =
            "4869205468657265",
        .digest =
            "b617318655057264e28bc0b6fb378c8e"
            "f146be00",
    },
};

static inline int unhex(char c)
{
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return c - '0';
}

static inline char hex(int i)
{
    if (i < 10) {
        return '0' + i;
    }
    return 'a' + (i - 10);
}

static size_t unhex_string(const char *hexstr,
                           uint8_t **data)
{
    size_t len;
    size_t i;

    if (!hexstr) {
        *data = NULL;
        return 0;
    }

    len = strlen(hexstr);
    *data = g_new0(uint8_t, len / 2);

    for (i = 0; i < len; i += 2) {
        (*data)[i / 2] = (unhex(hexstr[i]) << 4) | unhex(hexstr[i + 1]);
    }
    return len / 2;
}

static char *hex_string(const uint8_t *bytes,
                        size_t len)
{
    char *hexstr = g_new0(char, len * 2 + 1);
    size_t i;

    for (i = 0; i < len; i++) {
        hexstr[i * 2] = hex((bytes[i] >> 4) & 0xf);
        hexstr[i * 2 + 1] = hex(bytes[i] & 0xf);
    }
    hexstr[len * 2] = '\0';

    return hexstr;
}

static void test_hmac(const void *opaque)
{
    const QCryptoHmacTestData *data = opaque;
    size_t nkey, digest_len, msg_len;
    uint8_t *key = NULL;
    uint8_t *message = NULL;
    uint8_t *digest = NULL;
    uint8_t *output = NULL;
    char *outputhex = NULL;
    QCryptoHmac *hmac = NULL;
    Error *err = NULL;
    int ret;

    nkey = unhex_string(data->key, &key);
    digest_len = unhex_string(data->digest, &digest);
    msg_len = unhex_string(data->message, &message);

    output = g_new0(uint8_t, digest_len);

    hmac = qcrypto_hmac_new(data->alg, key, nkey, &err);
    g_assert(err == NULL);
    g_assert(hmac != NULL);

    ret = qcrypto_hmac_bytes(hmac, (const char *)message,
                        msg_len, &output, &digest_len, &err);

    g_assert(ret == 0);

    outputhex = hex_string(output, digest_len);

    g_assert_cmpstr(outputhex, ==, data->digest);

    qcrypto_hmac_free(hmac);

    g_free(outputhex);
    g_free(output);
    g_free(message);
    g_free(digest);
    g_free(key);
}

int main(int argc, char **argv)
{
    size_t i;

    g_test_init(&argc, &argv, NULL);

    g_assert(qcrypto_init(NULL) == 0);

    for (i = 0; i < G_N_ELEMENTS(test_data); i++) {
        g_test_add_data_func(test_data[i].path,
                    &test_data[i], test_hmac);
    }

    return g_test_run();
}
