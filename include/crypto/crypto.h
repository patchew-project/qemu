/*
 * QEMU Crypto Device Implement
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
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
#ifndef QCRYPTO_CRYPTO_H__
#define QCRYPTO_CRYPTO_H__

#include "qemu/queue.h"
#include "qapi-types.h"
#include "crypto/crypto-queue.h"

#define MAX_CRYPTO_QUEUE_NUM  64

#define  QEMU_CRYPTO_PACKET_FLAG_NONE (0)
#define  QEMU_CRYPTO_PACKET_FLAG_SYM (1 << 0)

typedef struct CryptoSymSessionInfo {
    uint8_t op_code;
    uint8_t op_type;
    uint8_t direction;
    uint32_t cipher_alg;
    uint32_t key_len;
    uint8_t *cipher_key;

    uint32_t hash_alg;
    uint8_t hash_mode;
    uint32_t hash_result_len;
    uint8_t alg_chain_order;
    uint32_t auth_key_len;
    uint32_t add_len;
    uint8_t *auth_key;
} CryptoSymSessionInfo;

typedef struct CryptoSymOpInfo {
    uint64_t session_id;
    uint8_t op_type; /* cipher or algo chainning */
    uint8_t *src;
    uint8_t *dst;
    uint8_t *iv;
    uint8_t *aad_data; /* additional auth data */
    uint32_t aad_len;
    uint32_t iv_len;
    uint32_t src_len;
    /* the dst_len is equal to src_len + hash_result_len
     * if hash alg configured */
    uint32_t dst_len;
    uint8_t data[0];
} CryptoSymOpInfo;

typedef void (CryptoPoll)(CryptoClientState *, bool);
typedef void (CryptoCleanup) (CryptoClientState *);
typedef void (CryptoClientDestructor)(CryptoClientState *);
typedef void (CryptoHWStatusChanged)(CryptoClientState *);
typedef int (CryptoCreateSymSession)(CryptoClientState *,
                              CryptoSymSessionInfo *, uint64_t *);
typedef int (CryptoCloseSession)(CryptoClientState *, uint64_t);
typedef int (CryptoDoSymOp)(CryptoClientState *, CryptoSymOpInfo *);

typedef struct CryptoClientInfo {
    CryptoClientOptionsKind type;
    size_t size;

    CryptoCleanup *cleanup;
    CryptoPoll *poll;
    CryptoHWStatusChanged *hw_status_changed;
    CryptoCreateSymSession *create_session;
    CryptoCloseSession *close_session;
    CryptoDoSymOp *do_sym_op;
} CryptoClientInfo;

struct CryptoClientState {
    CryptoClientInfo *info;
    int ready;
    QTAILQ_ENTRY(CryptoClientState) next;
    CryptoClientState *peer;
    char *model;
    char *name;
    char info_str[256];
    CryptoQueue *incoming_queue;
    unsigned int queue_index;

    /* Supported service mask */
    uint32_t crypto_services;

    /* Detailed algorithms mask */
    uint32_t cipher_algo_l;
    uint32_t cipher_algo_h;
    uint32_t hash_algo;
    uint32_t mac_algo_l;
    uint32_t mac_algo_h;
    uint32_t asym_algo;
    uint32_t kdf_algo;
    uint32_t aead_algo;
    uint32_t primitive_algo;

    CryptoClientDestructor *destructor;
};

/* qdev crypto legacy hardware properties */

typedef struct CryptoLegacyHWPeers {
    CryptoClientState *ccs[MAX_CRYPTO_QUEUE_NUM];
    int32_t queues;
} CryptoLegacyHWPeers;

typedef struct CryptoLegacyHWConf {
    CryptoLegacyHWPeers peers;

    /* Supported service mask */
    uint32_t crypto_services;

    /* Detailed algorithms mask */
    uint32_t cipher_algo_l;
    uint32_t cipher_algo_h;
    uint32_t hash_algo;
    uint32_t mac_algo_l;
    uint32_t mac_algo_h;
    uint32_t asym_algo;
    uint32_t kdf_algo;
    uint32_t aead_algo;
    uint32_t primitive_algo;
} CryptoLegacyHWConf;

typedef struct CryptoLegacyHWState {
    CryptoClientState *ccs;
    void *opaque;
    CryptoLegacyHWConf *conf;
    bool peer_deleted;
} CryptoLegacyHWState;

int crypto_client_init(QemuOpts *opts, Error **errp);
int crypto_init_clients(void);

CryptoClientState *new_crypto_client(CryptoClientInfo *info,
                                    CryptoClientState *peer,
                                    const char *model,
                                    const char *name);
int qemu_deliver_crypto_packet(CryptoClientState *sender,
                              unsigned flags,
                              void *header_opqaue,
                              void *opaque);
int qemu_send_crypto_packet_async(CryptoClientState *sender,
                                unsigned flags,
                                void *opaque,
                                CryptoPacketSent *sent_cb);
CryptoLegacyHWState *
qemu_new_crypto_legacy_hw(CryptoClientInfo *info,
                           CryptoLegacyHWConf *conf,
                           const char *model,
                           const char *name,
                           void *opaque);
void qemu_del_crypto_legacy_hw(CryptoLegacyHWState *crypto);

CryptoClientState *
qemu_get_crypto_subqueue(CryptoLegacyHWState *crypto, int queue_index);

CryptoLegacyHWState *qemu_get_crypto_legacy_hw(CryptoClientState *cc);

void *qemu_get_crypto_legacy_hw_opaque(CryptoClientState *cc);

int qemu_find_crypto_clients_except(const char *id, CryptoClientState **ccs,
                                 CryptoClientOptionsKind type, int max);

int qemu_crypto_create_session(CryptoClientState *cc,
                               CryptoSymSessionInfo *info,
                               uint64_t *session_id);
int qemu_crypto_close_session(CryptoClientState *cc,
                               uint64_t session_id);

#endif /* QCRYPTO_CRYPTO_H__ */
