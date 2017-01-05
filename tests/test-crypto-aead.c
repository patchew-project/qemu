/*
 * QEMU Crypto aead algorithms testcase
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
#include "crypto/aead.h"
#include "qapi/error.h"

typedef struct QCryptoAeadTestData QCryptoAeadTestData;
struct QCryptoAeadTestData {
    const char *path;
    QCryptoCipherAlgorithm alg;
    QCryptoCipherMode mode;
    const char *hex_key;
    const char *hex_nonce;
    const char *hex_aad;
    const char *hex_plain;
    const char *hex_cipher;
    const char *hex_tag;
};

static QCryptoAeadTestData test_data[] = {
    {
        /* Borrowed from libgcrypt */
        .path = "/crypto/aead/gcm-aes-128",
        .alg = QCRYPTO_CIPHER_ALG_AES_128,
        .mode = QCRYPTO_CIPHER_MODE_GCM,
        .hex_key = "0000000000000000"
                   "0000000000000000",
        .hex_nonce = "0000000000000000"
                     "00000000",
        .hex_aad = "",
        .hex_plain = "0000000000000000"
                     "0000000000000000",
        .hex_cipher = "0388dace60b6a392"
                      "f328c2b971b2fe78",
        .hex_tag = "ab6e47d42cec13bd"
                   "f53a67b21257bddf",
    },
    {
        /* Borrowed from libgcrypt */
        .path = "/crypto/aead/gcm-aes-192",
        .alg = QCRYPTO_CIPHER_ALG_AES_192,
        .mode = QCRYPTO_CIPHER_MODE_GCM,
        .hex_key = "feffe9928665731c"
                   "6d6a8f9467308308"
                   "feffe9928665731c",
        .hex_nonce = "9313225df88406e5"
                     "55909c5aff5269aa"
                     "6a7a9538534f7da1"
                     "e4c303d2a318a728"
                     "c3c0c95156809539"
                     "fcf0e2429a6b5254"
                     "16aedbf5a0de6a57"
                     "a637b39b",
        .hex_aad = "feedfacedeadbeef"
                   "feedfacedeadbeef"
                   "abaddad2",
        .hex_plain = "d9313225f88406e5"
                     "a55909c5aff5269a"
                     "86a7a9531534f7da"
                     "2e4c303d8a318a72"
                     "1c3c0c9595680953"
                     "2fcf0e2449a6b525"
                     "b16aedf5aa0de657"
                     "ba637b39",
        .hex_cipher = "d27e88681ce3243c"
                      "4830165a8fdcf9ff"
                      "1de9a1d8e6b447ef"
                      "6ef7b79828666e45"
                      "81e79012af34ddd9"
                      "e2f037589b292db3"
                      "e67c036745fa22e7"
                      "e9b7373b",
        .hex_tag = "dcf566ff291c25bb"
                   "b8568fc3d376a6d9",
    },
    {
        /* Borrowed from libgcrypt */
        .path = "/crypto/aead/gcm-aes-256",
        .alg = QCRYPTO_CIPHER_ALG_AES_256,
        .mode = QCRYPTO_CIPHER_MODE_GCM,
        .hex_key = "feffe9928665731c"
                   "6d6a8f9467308308"
                   "feffe9928665731c"
                   "6d6a8f9467308308",
        .hex_nonce = "9313225df88406e5"
                     "55909c5aff5269aa"
                     "6a7a9538534f7da1"
                     "e4c303d2a318a728"
                     "c3c0c95156809539"
                     "fcf0e2429a6b5254"
                     "16aedbf5a0de6a57"
                     "a637b39b",
        .hex_aad = "feedfacedeadbeef"
                   "feedfacedeadbeef"
                   "abaddad2",
        .hex_plain = "d9313225f88406e5"
                     "a55909c5aff5269a"
                     "86a7a9531534f7da"
                     "2e4c303d8a318a72"
                     "1c3c0c9595680953"
                     "2fcf0e2449a6b525"
                     "b16aedf5aa0de657"
                     "ba637b39",
        .hex_cipher = "5a8def2f0c9e53f1"
                      "f75d7853659e2a20"
                      "eeb2b22aafde6419"
                      "a058ab4f6f746bf4"
                      "0fc0c3b780f24445"
                      "2da3ebf1c5d82cde"
                      "a2418997200ef82e"
                      "44ae7e3f",
        .hex_tag = "a44a8266ee1c8eb0"
                   "c8b5d4cf5ae9f19a",
    },
    {
        /* Borrowed from libgcrypt */
        .path = "/crypto/aead/ccm-aes-128",
        .alg = QCRYPTO_CIPHER_ALG_AES_128,
        .mode = QCRYPTO_CIPHER_MODE_CCM,
        .hex_key = "c0c1c2c3c4c5c6c7"
                   "c8c9cacbcccdcecf",
        .hex_nonce = "00000003020100a0"
                     "a1a2a3a4a5",
        .hex_aad = "0001020304050607",
        .hex_plain = "08090a0b0c0d0e0f"
                     "1011121314151617"
                     "18191a1b1c1d1e",
        .hex_cipher = "588c979a61c663d2"
                      "f066d0c2c0f98980"
                      "6d5f6b61dac384",
        .hex_tag = "17e8d12cfdf926e0",
    },
    {
        .path = "/crypto/aead/ccm-aes-192",
        .alg = QCRYPTO_CIPHER_ALG_AES_192,
        .mode = QCRYPTO_CIPHER_MODE_CCM,
        .hex_key = "56df5c8f263f0e42"
                   "ef7ad3cefc846062"
                   "cab440af5fc9c901",
        .hex_nonce = "03d63c8c8684b6cd"
                     "ef092e94",
        .hex_aad = "0265783ce9213091"
                   "b1b9da769a786d95"
                   "f28832a3f250cb4c"
                   "e300736984698779",
        .hex_plain = "9fd2024b5249313c"
                     "43693a2d8e70ad7e"
                     "e0e54609808913b2"
                     "8c8bd93f86fbb56b",
        .hex_cipher = "00161ecf83e37c91"
                      "ce8bdb138370e37a"
                      "d638efed5e3a8aed"
                      "1841db9f8654251d",
        .hex_tag = "18219f9396f03723"
                   "c185f9781ec0a6ad",
    },
    {
        /* Borrowed from nettle*/
        .path = "/crypto/aead/ccm-aes-256",
        .alg = QCRYPTO_CIPHER_ALG_AES_256,
        .mode = QCRYPTO_CIPHER_MODE_CCM,
        .hex_key = "4041424344454647"
                   "48494a4b4c4d4e4f"
                   "5051525354555657"
                   "58595a5b5c5d5e5f",
        .hex_nonce = "1011121314151617"
                     "18191a1b",
        .hex_aad = "0001020304050607"
                   "08090a0b0c0d0e0f"
                   "10111213",
        .hex_plain = "2021222324252627"
                     "28292a2b2c2d2e2f"
                     "3031323334353637",
        .hex_cipher = "04f883aeb3bd0730"
                      "eaf50bb6de4fa221"
                      "2034e4e41b0e75e5",
        .hex_tag = "9bba3f3a107f3239"
                   "bd63902923f80371",
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
        (*data)[i / 2] = (unhex(hexstr[i]) << 4)
                          | unhex(hexstr[i + 1]);
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

static void _test_aead(const QCryptoAeadTestData *data,
                       bool encrypt)
{
    QCryptoAead *aead;

    uint8_t *key = NULL, *nonce = NULL;
    uint8_t *aad = NULL, *in = NULL;
    size_t key_len = 0, nonce_len = 0, aad_len = 0;
    size_t in_len = 0, tag_len = 0;
    uint8_t *hex_tmp = NULL;
    uint8_t *out = NULL;
    uint8_t out_len = 0;
    Error *err = NULL;
    int ret;

    key_len = unhex_string(data->hex_key, &key);
    nonce_len = unhex_string(data->hex_nonce, &nonce);
    aad_len = unhex_string(data->hex_aad, &aad);
    if (encrypt) {
        in_len = unhex_string(data->hex_plain, &in);
    } else {
        in_len = unhex_string(data->hex_cipher, &in);
    }

    tag_len = strlen(data->hex_tag) / 2;
    out_len = in_len + tag_len;
    out = g_new0(uint8_t, out_len);

    aead = qcrypto_aead_new(data->alg, data->mode,
                            key, key_len, &err);
    g_assert(aead != NULL);

    ret = qcrypto_aead_set_nonce(aead, nonce, nonce_len,
                                 aad_len, in_len, tag_len,
                                 &err);
    g_assert(ret == 0);

    ret = qcrypto_aead_authenticate(aead, aad, aad_len,
                                    &err);
    g_assert(ret == 0);

    if (encrypt) {
        ret = qcrypto_aead_encrypt(aead, in, in_len,
                                   out, in_len, &err);
        g_assert(ret == 0);

        hex_tmp = (uint8_t *)hex_string(out, in_len);
        g_assert_cmpstr((char *)hex_tmp, ==,
                        (char *)data->hex_cipher);
        g_free(hex_tmp);
    } else {
        ret = qcrypto_aead_decrypt(aead, in, in_len,
                                   out, in_len, &err);
        g_assert(ret == 0);

        hex_tmp = (uint8_t *)hex_string(out, in_len);
        g_assert_cmpstr((char *)hex_tmp, ==,
                        (char *)data->hex_plain);
        g_free(hex_tmp);
    }

    ret = qcrypto_aead_get_tag(aead, out + in_len,
                               tag_len, &err);
    g_assert(ret == 0);

    hex_tmp = (uint8_t *)hex_string(out + in_len,
                                    tag_len);
    g_assert_cmpstr((char *)hex_tmp, ==,
                    (char *)data->hex_tag);
    g_free(hex_tmp);

    g_free(out);
    g_free(in);
    g_free(aad);
    g_free(nonce);
    g_free(key);
    qcrypto_aead_free(aead);
}

static void test_aead(const void *opaque)
{
    const QCryptoAeadTestData *data = opaque;

    /* test encrypt */
    _test_aead(data, 1);

    /* test decrypt */
    _test_aead(data, 0);
}

int main(int argc, char **argv)
{
    size_t i;

    g_test_init(&argc, &argv, NULL);

    g_assert(qcrypto_init(NULL) == 0);

    for (i = 0; i < G_N_ELEMENTS(test_data); i++) {
        if (qcrypto_aead_supports(test_data[i].alg,
                                  test_data[i].mode)) {
            g_test_add_data_func(test_data[i].path,
                                 &test_data[i], test_aead);
        }
    }

    return g_test_run();
}
