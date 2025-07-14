/*
 * S/390 DIAGNOSE 508 definitions and structures
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Collin Walling <walling@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef S390X_DIAG508_H
#define S390X_DIAG508_H

#define DIAG_508_SUBC_QUERY_SUBC    0x0000
#define DIAG_508_SUBC_SIG_VERIF     0x8000

#define DIAG_508_RC_OK              0x0001
#define DIAG_508_RC_NO_CERTS        0x0102
#define DIAG_508_RC_INVAL_COMP_DATA 0x0202
#define DIAG_508_RC_INVAL_PKCS7_SIG 0x0302
#define DIAG_508_RC_FAIL_VERIF      0x0402

struct Diag508CertificateStoreInfo {
    uint8_t  idx;
    uint8_t  reserved[7];
    uint64_t len;
};
typedef struct Diag508CertificateStoreInfo Diag508CertificateStoreInfo;

struct Diag508SignatureVerificationBlock {
    Diag508CertificateStoreInfo csi;
    uint64_t comp_len;
    uint64_t comp_addr;
    uint64_t sig_len;
    uint64_t sig_addr;
};
typedef struct Diag508SignatureVerificationBlock Diag508SignatureVerificationBlock;

#endif
