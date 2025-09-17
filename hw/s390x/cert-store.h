/*
 * S390 certificate store
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_S390_CERT_STORE_H
#define HW_S390_CERT_STORE_H

#include "hw/s390x/ipl/qipl.h"
#include "crypto/x509-utils.h"

#define VC_NAME_LEN_BYTES  64

#define CERT_KEY_ID_LEN    QCRYPTO_HASH_DIGEST_LEN_SHA256
#define CERT_HASH_LEN      QCRYPTO_HASH_DIGEST_LEN_SHA256

struct S390IPLCertificate {
    uint8_t vc_name[VC_NAME_LEN_BYTES];
    size_t  size;
    size_t  der_size;
    uint8_t *raw;
};
typedef struct S390IPLCertificate S390IPLCertificate;

struct S390IPLCertificateStore {
    uint16_t count;
    size_t   max_cert_size;
    size_t   total_bytes;
    S390IPLCertificate certs[MAX_CERTIFICATES];
} QEMU_PACKED;
typedef struct S390IPLCertificateStore S390IPLCertificateStore;

void s390_ipl_create_cert_store(S390IPLCertificateStore *cert_store);

#endif
