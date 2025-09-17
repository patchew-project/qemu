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

static inline void zipl_secure_handle(const char *message)
{
    switch (boot_mode) {
    case ZIPL_BOOT_MODE_SECURE_AUDIT:
        IPL_check(false, message);
        break;
    default:
        break;
    }
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

static inline bool is_vce_cert_valid(uint8_t vce_flags, uint32_t vce_len)
{
    return (vce_flags & DIAG_320_VCE_FLAGS_VALID) && (vce_len > VCE_INVALID_LEN);
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
