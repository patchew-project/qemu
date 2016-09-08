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

typedef void (CryptoPoll)(CryptoClientState *, bool);
typedef void (CryptoCleanup) (CryptoClientState *);
typedef void (CryptoClientDestructor)(CryptoClientState *);
typedef void (CryptoHWStatusChanged)(CryptoClientState *);

typedef struct CryptoClientInfo {
    CryptoClientOptionsKind type;
    size_t size;

    CryptoCleanup *cleanup;
    CryptoPoll *poll;
    CryptoHWStatusChanged *hw_status_changed;
} CryptoClientInfo;

struct CryptoClientState {
    CryptoClientInfo *info;
    int ready;
    QTAILQ_ENTRY(CryptoClientState) next;
    CryptoClientState *peer;
    char *model;
    char *name;
    char info_str[256];
    CryptoClientDestructor *destructor;
};

int crypto_client_init(QemuOpts *opts, Error **errp);
int crypto_init_clients(void);

CryptoClientState *new_crypto_client(CryptoClientInfo *info,
                                    CryptoClientState *peer,
                                    const char *model,
                                    const char *name);

#endif /* QCRYPTO_CRYPTO_H__ */
