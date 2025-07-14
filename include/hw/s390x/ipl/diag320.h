/*
 * S/390 DIAGNOSE 320 definitions and structures
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef S390X_DIAG320_H
#define S390X_DIAG320_H

#define DIAG_320_SUBC_QUERY_ISM     0
#define DIAG_320_SUBC_QUERY_VCSI    1
#define DIAG_320_SUBC_STORE_VC      2

#define DIAG_320_RC_OK              0x0001
#define DIAG_320_RC_INVAL_VCB_LEN   0x0204
#define DIAG_320_RC_BAD_RANGE       0x0302

#define VCSSB_MAX_LEN   128
#define VCE_HEADER_LEN  128
#define VCB_HEADER_LEN  64

#define DIAG_320_ISM_QUERY_VCSI     0x4000000000000000
#define DIAG_320_ISM_STORE_VC       0x2000000000000000

#define DIAG_320_VCE_FLAGS_VALID                0x80
#define DIAG_320_VCE_KEYTYPE_SELF_DESCRIBING    0
#define DIAG_320_VCE_FORMAT_X509_DER            1
#define DIAG_320_VCE_HASHTYPE_SHA2_256          1

struct VCStorageSizeBlock {
    uint32_t length;
    uint8_t reserved0[3];
    uint8_t version;
    uint32_t reserved1[6];
    uint16_t total_vc_ct;
    uint16_t max_vc_ct;
    uint32_t reserved3[7];
    uint32_t max_vce_len;
    uint32_t reserved4[3];
    uint32_t max_single_vcb_len;
    uint32_t total_vcb_len;
    uint32_t reserved5[10];
};
typedef struct VCStorageSizeBlock VCStorageSizeBlock;

struct VCBlock {
    uint32_t in_len;
    uint32_t reserved0;
    uint16_t first_vc_index;
    uint16_t last_vc_index;
    uint32_t reserved1[5];
    uint32_t out_len;
    uint8_t reserved2[3];
    uint8_t version;
    uint16_t stored_ct;
    uint16_t remain_ct;
    uint32_t reserved3[5];
    uint8_t vce_buf[];
};
typedef struct VCBlock VCBlock;

struct VCEntry {
    uint32_t len;
    uint8_t flags;
    uint8_t key_type;
    uint16_t cert_idx;
    uint32_t name[16];
    uint8_t format;
    uint8_t reserved0;
    uint16_t keyid_len;
    uint8_t reserved1;
    uint8_t hash_type;
    uint16_t hash_len;
    uint32_t reserved2;
    uint32_t cert_len;
    uint32_t reserved3[2];
    uint16_t hash_offset;
    uint16_t cert_offset;
    uint32_t reserved4[7];
    uint8_t cert_buf[];
};
typedef struct VCEntry VCEntry;

#endif
