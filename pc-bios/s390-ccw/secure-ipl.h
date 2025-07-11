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
uint32_t zipl_secure_get_certs_length(void);
uint32_t zipl_secure_request_certificate(uint64_t *cert, uint8_t index);
void zipl_secure_cert_list_add(IplSignatureCertificateList *certs, int cert_index,
                               uint64_t *cert, uint64_t cert_len);
void zipl_secure_comp_list_add(IplDeviceComponentList *comps, int comp_index,
                               int cert_index, uint64_t comp_addr,
                               uint64_t comp_len, uint8_t flags);
int zipl_secure_update_iirb(IplDeviceComponentList *comps,
                            IplSignatureCertificateList *certs);
bool zipl_secure_ipl_supported(void);
void zipl_secure_init_lists(IplDeviceComponentList *comps,
                            IplSignatureCertificateList *certs);

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

void zipl_secure_addr_overlap_check(SecureIplCompAddrRange *comp_addr_range,
                                    int *addr_range_index,
                                    uint64_t start_addr, uint64_t end_addr,
                                    bool is_signed);
void zipl_secure_check_signed_comp(int signed_count, IplDeviceComponentList *comps);
void zipl_secure_check_sclab_count(int count, IplDeviceComponentList *comps);
void zipl_secure_check_global_sclab(SecureIplSclabInfo sclab_info,
                                    SecureIplCompAddrRange *comp_addr_range,
                                    int addr_range_index, uint64_t load_psw,
                                    int unsigned_count, int signed_count,
                                    IplDeviceComponentList *comps, int comp_index);
void zipl_secure_check_unsigned_comp(uint64_t comp_addr, IplDeviceComponentList *comps,
                                     int comp_index, int cert_index, uint64_t comp_len);
void zipl_secure_check_sclab(uint64_t comp_addr, IplDeviceComponentList *comps,
                             uint64_t comp_len, int comp_index,
                             SecureIplSclabInfo *sclab_info);

static inline void zipl_secure_print(const char *message)
{
    switch (boot_mode) {
    case ZIPL_SECURE_AUDIT_MODE:
        IPL_check(false, message);
        break;
    default:
        break;
    }
}

static inline bool is_sclab_flag_set(uint16_t sclab_flags, uint16_t flag)
{
    return (sclab_flags & flag) != 0;
}

static inline bool validate_unsigned_addr(uint64_t comp_load_addr)
{
    /* usigned load address must be greater than or equal to 0x2000 */
    return comp_load_addr >= 0x2000;
}

static inline bool validate_sclab_magic(uint8_t *sclab_magic)
{
    /* identifies the presence of SCLAB */
    return magic_match(sclab_magic, ZIPL_MAGIC);
}

static inline bool validate_sclab_length(uint16_t sclab_len)
{
    /* minimum SCLAB length is 32 bytes */
    return sclab_len >= 32;
}

static inline bool validate_sclab_format(uint8_t sclab_format)
{
    /* SCLAB format must set to zero, indicating a format-0 SCLAB being used */
    return sclab_format == 0;
}

static inline bool validate_sclab_ola_zero(uint64_t sclab_load_addr)
{
    /* Load address field in SCLAB must contain zeros */
    return sclab_load_addr == 0;
}

static inline bool validate_sclab_ola_one(uint64_t sclab_load_addr,
                                          uint64_t comp_load_addr)
{
   /* Load address field must match storage address of the component */
   return sclab_load_addr == comp_load_addr;
}

static inline bool validate_sclab_opsw_zero(uint64_t sclab_load_psw)
{
    /* Load PSW field in SCLAB must contain zeros */
    return sclab_load_psw == 0;
}

static inline bool validate_sclab_opsw_one(uint16_t sclab_flags)
{
   /* OLA must set to one */
   return is_sclab_flag_set(sclab_flags, S390_SECURE_IPL_SCLAB_FLAG_OLA);
}

static inline bool validate_lpsw(uint64_t sclab_load_psw, uint64_t comp_load_psw)
{
    /* compare load PSW with the PSW specified in component */
    return sclab_load_psw == comp_load_psw;
}

static inline void set_cei_with_log(IplDeviceComponentList *comps, int comp_index,
                                    uint32_t flag, const char *message)
{
    comps->device_entries[comp_index].cei |= flag;
    zipl_secure_print(message);
}

static inline void set_iiei_with_log(IplDeviceComponentList *comps, uint16_t flag,
                                     const char *message)
{
    comps->ipl_info_header.iiei |= flag;
    zipl_secure_print(message);
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
    uint64_t d320_ism;

    diag320(&d320_ism, DIAG_320_SUBC_QUERY_ISM);
    return (d320_ism & DIAG_320_ISM_QUERY_VCSI) &&
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
    Diag508SignatureVerificationBlock svb = {{}, comp_len, comp_addr,
                                             sig_len, sig_addr };

    if (_diag508(&svb, DIAG_508_SUBC_SIG_VERIF) == DIAG_508_RC_OK) {
        *cert_len = svb.csi.len;
        *cert_idx = svb.csi.idx;
        return true;
    }

    return false;
}

#endif /* _PC_BIOS_S390_CCW_SECURE_IPL_H */
