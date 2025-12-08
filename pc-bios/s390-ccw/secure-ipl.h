/*
 * S/390 Secure IPL
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _PC_BIOS_S390_CCW_SECURE_IPL_H
#define _PC_BIOS_S390_CCW_SECURE_IPL_H

#include <diag320.h>
#include <diag508.h>

VCStorageSizeBlock *zipl_secure_get_vcssb(void);
int zipl_run_secure(ComponentEntry **entry_ptr, uint8_t *tmp_sec);

#define S390_SECURE_IPL_SCLAB_FLAG_OPSW    0x8000
#define S390_SECURE_IPL_SCLAB_FLAG_OLA     0x4000
#define S390_SECURE_IPL_SCLAB_FLAG_NUC     0x2000
#define S390_SECURE_IPL_SCLAB_FLAG_SC      0x1000

struct SecureCodeLoadingAttributesBlock {
    uint8_t  format;
    uint8_t  reserved1;
    uint16_t flags;
    uint8_t  reserved2[4];
    uint64_t load_psw;
    uint64_t load_addr;
    uint64_t reserved3[];
} __attribute__ ((packed));
typedef struct SecureCodeLoadingAttributesBlock SecureCodeLoadingAttributesBlock;

struct SclabOriginLocator {
    uint8_t reserved[2];
    uint16_t len;
    uint8_t magic[4];
} __attribute__ ((packed));
typedef struct SclabOriginLocator SclabOriginLocator;

typedef struct SecureIplCompAddrRange {
    bool is_signed;
    uint64_t start_addr;
    uint64_t end_addr;
} SecureIplCompAddrRange;

typedef struct SecureIplSclabInfo {
    int count;
    int global_count;
    uint64_t load_psw;
    uint16_t flags;
} SecureIplSclabInfo;

static inline void zipl_secure_handle(const char *message)
{
    switch (boot_mode) {
    case ZIPL_BOOT_MODE_SECURE_AUDIT:
        IPL_check(false, message);
        break;
    case ZIPL_BOOT_MODE_SECURE:
        panic(message);
        break;
    default:
        break;
    }
}

static inline bool is_sclab_flag_set(uint16_t sclab_flags, uint16_t flag)
{
    return (sclab_flags & flag) != 0;
}

static inline void set_cei_with_log(IplDeviceComponentList *comps, int comp_index,
                                    uint32_t flag, const char *message)
{
    comps->device_entries[comp_index].cei |= flag;
    zipl_secure_handle(message);
}

static inline void set_iiei_with_log(IplDeviceComponentList *comps, uint16_t flag,
                                     const char *message)
{
    comps->ipl_info_header.iiei |= flag;
    zipl_secure_handle(message);
}

static inline uint64_t diag320(void *data, unsigned long subcode)
{
    register unsigned long addr asm("0") = (unsigned long)data;
    register unsigned long rc asm("1") = 0;

    asm volatile ("diag %0,%2,0x320\n"
                  : "+d" (addr), "+d" (rc)
                  : "d" (subcode)
                  : "memory", "cc");
    return rc;
}

static inline bool is_cert_store_facility_supported(void)
{
    uint32_t d320_ism;

    diag320(&d320_ism, DIAG_320_SUBC_QUERY_ISM);
    return (d320_ism & DIAG_320_ISM_QUERY_SUBCODES) &&
           (d320_ism & DIAG_320_ISM_QUERY_VCSI) &&
           (d320_ism & DIAG_320_ISM_STORE_VC);
}

static inline uint64_t _diag508(void *data, unsigned long subcode)
{
    register unsigned long addr asm("0") = (unsigned long)data;
    register unsigned long rc asm("1") = 0;

    asm volatile ("diag %0,%2,0x508\n"
                  : "+d" (addr), "+d" (rc)
                  : "d" (subcode)
                  : "memory", "cc");
    return rc;
}

static inline bool is_secure_ipl_extension_supported(void)
{
    uint64_t d508_subcodes;

    d508_subcodes = _diag508(NULL, DIAG_508_SUBC_QUERY_SUBC);
    return d508_subcodes & DIAG_508_SUBC_SIG_VERIF;
}

static inline bool verify_signature(uint64_t comp_len, uint64_t comp_addr,
                                    uint64_t sig_len, uint64_t sig_addr,
                                    uint64_t *cert_len, uint8_t *cert_idx)
{
    Diag508SigVerifBlock svb;

    svb.length = sizeof(Diag508SigVerifBlock);
    svb.version = 0;
    svb.comp_len = comp_len;
    svb.comp_addr = comp_addr;
    svb.sig_len = sig_len;
    svb.sig_addr = sig_addr;

    if (_diag508(&svb, DIAG_508_SUBC_SIG_VERIF) == DIAG_508_RC_OK) {
        *cert_len = svb.cert_len;
        *cert_idx = svb.cert_store_index;
        return true;
    }

    return false;
}

#endif /* _PC_BIOS_S390_CCW_SECURE_IPL_H */
