/*
 * S/390 Secure IPL
 *
 * Functions to support IPL in secure boot mode (DIAG 320, DIAG 508,
 * signature verification, and certificate handling).
 *
 * For secure IPL overview: docs/system/s390x/secure-ipl.rst
 * For secure IPL technical: docs/specs/s390x-secure-ipl.rst
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "bootmap.h"
#include "s390-ccw.h"
#include "secure-ipl.h"

uint8_t vcssb_data[VCSSB_MIN_LEN] __attribute__((__aligned__(PAGE_SIZE)));

VCStorageSizeBlock *zipl_secure_get_vcssb(void)
{
    VCStorageSizeBlock *vcssb;
    int rc;

    if (!(sclp_is_diag320_on() && is_cert_store_facility_supported())) {
        puts("Certificate Store Facility is not supported by the hypervisor!");
        return NULL;
    }

    vcssb = (VCStorageSizeBlock *)vcssb_data;
    /* avoid retrieving vcssb multiple times */
    if (vcssb->length >= VCSSB_MIN_LEN) {
        return vcssb;
    }

    vcssb->length = VCSSB_MIN_LEN;
    rc = diag320(vcssb, DIAG_320_SUBC_QUERY_VCSI);
    if (rc != DIAG_320_RC_OK) {
        return NULL;
    }

    return vcssb;
}

static uint32_t get_certs_length(void)
{
    VCStorageSizeBlock *vcssb;
    uint32_t len;

    vcssb = zipl_secure_get_vcssb();
    if (vcssb == NULL) {
        return 0;
    }

    len = vcssb->total_vcb_len - VCB_HEADER_LEN - vcssb->total_vc_ct * VCE_HEADER_LEN;

    return len;
}

static uint32_t request_certificate(uint8_t *cert, uint8_t index)
{
    VCStorageSizeBlock *vcssb;
    VCBlock *vcb;
    VCEntry *vce;
    uint64_t rc = 0;
    uint32_t cert_len = 0;

    /* Get Verification Certificate Storage Size block with DIAG320 subcode 1 */
    vcssb = zipl_secure_get_vcssb();
    if (vcssb == NULL) {
        return 0;
    }

    /*
     * Request single entry
     * Fill input fields of single-entry VCB
     */
    vcb = malloc(MAX_SECTOR_SIZE * 4);
    vcb->in_len = ROUND_UP(vcssb->max_single_vcb_len, PAGE_SIZE);
    vcb->first_vc_index = index + 1;
    vcb->last_vc_index = index + 1;

    rc = diag320(vcb, DIAG_320_SUBC_STORE_VC);
    if (rc == DIAG_320_RC_OK) {
        if (vcb->out_len == VCB_HEADER_LEN) {
            puts("No certificate entry");
            goto out;
        }
        if (vcb->remain_ct != 0) {
            puts("Not enough memory to store all requested certificates");
            goto out;
        }

        vce = (VCEntry *)vcb->vce_buf;
        if (!is_vce_cert_valid(vce->flags, vce->len)) {
            puts("Invalid certificate");
            goto out;
        }

        cert_len = vce->cert_len;
        memcpy(cert, (uint8_t *)vce + vce->cert_offset, vce->cert_len);
    }

out:
    free(vcb);
    return cert_len;
}

static void cert_list_add(IplSignatureCertificateList *certs, int cert_index,
                          uint8_t *cert, uint64_t cert_len)
{
    if (cert_index > MAX_CERTIFICATES - 1) {
        printf("Warning: Ignoring cert entry [%d] because it's over %d entires\n",
                cert_index + 1, MAX_CERTIFICATES);
        return;
    }

    certs->cert_entries[cert_index].addr = (uint64_t)cert;
    certs->cert_entries[cert_index].len = cert_len;
    certs->ipl_info_header.len += sizeof(certs->cert_entries[cert_index]);
}

static void comp_list_add(IplDeviceComponentList *comps, int comp_index,
                          int cert_index, uint64_t comp_addr,
                          uint64_t comp_len, uint8_t flags)
{
    if (comp_index > MAX_CERTIFICATES - 1) {
        printf("Warning: Ignoring comp entry [%d] because it's over %d entires\n",
                comp_index + 1, MAX_CERTIFICATES);
        return;
    }

    comps->device_entries[comp_index].addr = comp_addr;
    comps->device_entries[comp_index].len = comp_len;
    comps->device_entries[comp_index].flags = flags;
    comps->device_entries[comp_index].cert_index = cert_index;
    comps->ipl_info_header.len += sizeof(comps->device_entries[comp_index]);
}

static int update_iirb(IplDeviceComponentList *comps, IplSignatureCertificateList *certs)
{
    IplInfoReportBlock *iirb;
    IplDeviceComponentList *iirb_comps;
    IplSignatureCertificateList *iirb_certs;
    uint32_t iirb_hdr_len;
    uint32_t comps_len;
    uint32_t certs_len;

    if (iplb->len % 8 != 0) {
        panic("IPL parameter block length field value is not multiple of 8 bytes");
    }

    iirb_hdr_len = sizeof(IplInfoReportBlockHeader);
    comps_len = comps->ipl_info_header.len;
    certs_len = certs->ipl_info_header.len;
    if ((comps_len + certs_len + iirb_hdr_len) > sizeof(IplInfoReportBlock)) {
        puts("Not enough space to hold all components and certificates in IIRB");
        return -1;
    }

    /* IIRB immediately follows IPLB */
    iirb = &ipl_data.iirb;
    iirb->hdr.len = iirb_hdr_len;

    /* Copy IPL device component list after IIRB Header */
    iirb_comps = (IplDeviceComponentList *) iirb->info_blks;
    memcpy(iirb_comps, comps, comps_len);

    /* Update IIRB length */
    iirb->hdr.len += comps_len;

    /* Copy IPL sig cert list after IPL device component list */
    iirb_certs = (IplSignatureCertificateList *) (iirb->info_blks +
                                                  iirb_comps->ipl_info_header.len);
    memcpy(iirb_certs, certs, certs_len);

    /* Update IIRB length */
    iirb->hdr.len += certs_len;

    return 0;
}

static bool secure_ipl_supported(void)
{
    if (!sclp_is_sipl_on()) {
        puts("Secure IPL Facility is not supported by the hypervisor!");
        return false;
    }

    if (!is_secure_ipl_extension_supported()) {
        puts("Secure IPL extensions are not supported by the hypervisor!");
        return false;
    }

    if (!(sclp_is_diag320_on() && is_cert_store_facility_supported())) {
        puts("Certificate Store Facility is not supported by the hypervisor!");
        return false;
    }

    if (!sclp_is_sclaf_on()) {
        puts("Secure IPL Code Loading Attributes Facility is not supported by" \
             " the hypervisor!");
        return false;
    }

    return true;
}

static void init_lists(IplDeviceComponentList *comps, IplSignatureCertificateList *certs)
{
    comps->ipl_info_header.ibt = IPL_IBT_COMPONENTS;
    comps->ipl_info_header.len = sizeof(comps->ipl_info_header);

    certs->ipl_info_header.ibt = IPL_IBT_CERTIFICATES;
    certs->ipl_info_header.len = sizeof(certs->ipl_info_header);
}

static bool is_comp_overlap(SecureIplCompAddrRange *comp_addr_range, int addr_range_index,
                            uint64_t start_addr, uint64_t end_addr)
{
    /* neither a signed nor an unsigned component can overlap with a signed component */
    for (int i = 0; i < addr_range_index; i++) {
        if ((comp_addr_range[i].start_addr <= end_addr &&
            start_addr <= comp_addr_range[i].end_addr) &&
            comp_addr_range[i].is_signed) {
            return true;
       }
    }

    return false;
}

static void comp_addr_range_add(SecureIplCompAddrRange *comp_addr_range,
                                int addr_range_index, bool is_signed,
                                uint64_t start_addr, uint64_t end_addr)
{
    if (addr_range_index > MAX_CERTIFICATES - 1) {
        return;
    }

    comp_addr_range[addr_range_index].is_signed = is_signed;
    comp_addr_range[addr_range_index].start_addr = start_addr;
    comp_addr_range[addr_range_index].end_addr = end_addr;
}

static void check_unsigned_addr(uint64_t load_addr, IplDeviceComponentList *comps,
                                int comp_index)
{
    uint32_t flag;
    const char *msg;
    bool valid;

    valid = validate_unsigned_addr(load_addr);
    if (!valid) {
        flag = S390_IPL_COMPONENT_CEI_INVALID_UNSIGNED_ADDR;
        msg = "Load address is less than 0x2000";
        set_cei_with_log(comps, comp_index, flag, msg);
    }
}

static void addr_overlap_check(SecureIplCompAddrRange *comp_addr_range,
                               int *addr_range_index,
                               uint64_t start_addr, uint64_t end_addr, bool is_signed)
{
    bool overlap;

    overlap = is_comp_overlap(comp_addr_range, *addr_range_index,
                              start_addr, end_addr);
    if (!overlap) {
        comp_addr_range_add(comp_addr_range, *addr_range_index, is_signed,
                            start_addr, end_addr);
        *addr_range_index += 1;
    } else {
        zipl_secure_handle("Component addresses overlap");
    }
}

static bool check_sclab_presence(uint8_t *sclab_magic,
                                 IplDeviceComponentList *comps, int comp_index)
{
    if (!validate_sclab_magic(sclab_magic)) {
        comps->device_entries[comp_index].cei |= S390_IPL_COMPONENT_CEI_INVALID_SCLAB;

        /* a missing SCLAB will not be reported in audit mode */
        if (boot_mode == ZIPL_BOOT_MODE_SECURE) {
            zipl_secure_handle("Magic is not matched. SCLAB does not exist");
         }

        return false;
    }

    return true;
}

static void check_sclab_length(uint16_t sclab_len,
                               IplDeviceComponentList *comps, int comp_index)
{
    const char *msg;
    uint32_t flag;
    bool valid;

    valid = validate_sclab_length(sclab_len);
    if (!valid) {
        flag = S390_IPL_COMPONENT_CEI_INVALID_SCLAB_LEN |
               S390_IPL_COMPONENT_CEI_INVALID_SCLAB;
        msg = "Invalid SCLAB length";
        set_cei_with_log(comps, comp_index, flag, msg);
    }
}

static void check_sclab_format(uint8_t sclab_format,
                               IplDeviceComponentList *comps, int comp_index)
{
    const char *msg;
    uint32_t flag;
    bool valid;

    valid = validate_sclab_format(sclab_format);
    if (!valid) {
        flag = S390_IPL_COMPONENT_CEI_INVALID_SCLAB_FORMAT;
        msg = "Format-0 SCLAB is not being use";
        set_cei_with_log(comps, comp_index, flag, msg);
    }
}

static void check_sclab_opsw(SecureCodeLoadingAttributesBlock *sclab,
                             SecureIplSclabInfo *sclab_info,
                             IplDeviceComponentList *comps, int comp_index)
{
    const char *msg;
    uint32_t flag;
    bool is_opsw_set;
    bool valid;

    is_opsw_set = is_sclab_flag_set(sclab->flags, S390_SECURE_IPL_SCLAB_FLAG_OPSW);
    if (!is_opsw_set) {
        valid = validate_sclab_opsw_zero(sclab->load_psw);
        if (!valid) {
            flag = S390_IPL_COMPONENT_CEI_SCLAB_LOAD_PSW_NOT_ZERO;
            msg = "Load PSW is not zero when Override PSW bit is zero";
            set_cei_with_log(comps, comp_index, flag, msg);
        }
    } else {
        /* OPSW = 1 indicating global SCLAB */
        valid = validate_sclab_opsw_one(sclab->flags);
        if (!valid) {
            flag = S390_IPL_COMPONENT_CEI_SCLAB_OLA_NOT_ONE;
            msg = "Override Load Address bit is not set to one in the global SCLAB";
            set_cei_with_log(comps, comp_index, flag, msg);
        }

        sclab_info->global_count += 1;
        if (sclab_info->global_count == 1) {
            sclab_info->load_psw = sclab->load_psw;
            sclab_info->flags = sclab->flags;
        }
    }
}

static void check_sclab_ola(SecureCodeLoadingAttributesBlock *sclab,
                            uint64_t load_addr, IplDeviceComponentList *comps,
                            int comp_index)
{
    const char *msg;
    uint32_t flag;
    bool is_ola_set;
    bool valid;

    is_ola_set = is_sclab_flag_set(sclab->flags, S390_SECURE_IPL_SCLAB_FLAG_OLA);
    if (!is_ola_set) {
        valid = validate_sclab_ola_zero(sclab->load_addr);
        if (!(valid)) {
            flag = S390_IPL_COMPONENT_CEI_SCLAB_LOAD_ADDR_NOT_ZERO;
            msg = "Load Address is not zero when Override Load Address bit is zero";
            set_cei_with_log(comps, comp_index, flag, msg);
        }

    } else {
        valid = validate_sclab_ola_one(sclab->load_addr, load_addr);
        if (!valid) {
            flag = S390_IPL_COMPONENT_CEI_UNMATCHED_SCLAB_LOAD_ADDR;
            msg = "Load Address does not match with component load address";
            set_cei_with_log(comps, comp_index, flag, msg);
        }
    }
}

static void check_sclab_nuc(uint16_t sclab_flags, IplDeviceComponentList *comps,
                            int comp_index)
{
    const char *msg;
    uint32_t flag;
    bool is_nuc_set;
    bool is_global_sclab;

    is_nuc_set = is_sclab_flag_set(sclab_flags, S390_SECURE_IPL_SCLAB_FLAG_NUC);
    is_global_sclab = is_sclab_flag_set(sclab_flags, S390_SECURE_IPL_SCLAB_FLAG_OPSW);
    if (is_nuc_set && !is_global_sclab) {
        flag = S390_IPL_COMPONENT_CEI_NUC_NOT_IN_GLOBAL_SCLA;
        msg = "No Unsigned Components bit is set, but not in the global SCLAB";
        set_cei_with_log(comps, comp_index, flag, msg);
    }
}

static void check_sclab_sc(uint16_t sclab_flags, IplDeviceComponentList *comps,
                           int comp_index)
{
    const char *msg;
    uint32_t flag;
    bool is_sc_set;
    bool is_global_sclab;

    is_sc_set = is_sclab_flag_set(sclab_flags, S390_SECURE_IPL_SCLAB_FLAG_SC);
    is_global_sclab = is_sclab_flag_set(sclab_flags, S390_SECURE_IPL_SCLAB_FLAG_OPSW);
    if (is_sc_set && !is_global_sclab) {
        flag = S390_IPL_COMPONENT_CEI_SC_NOT_IN_GLOBAL_SCLAB;
        msg = "Single Component bit is set, but not in the global SCLAB";
        set_cei_with_log(comps, comp_index, flag, msg);
    }
}

static bool is_psw_valid(uint64_t psw, SecureIplCompAddrRange *comp_addr_range,
                         int range_index)
{
    uint32_t addr = psw & 0x3FFFFFFF;

    /* PSW points within a signed binary code component */
    for (int i = 0; i < range_index; i++) {
        if (comp_addr_range[i].is_signed &&
            addr >= comp_addr_range[i].start_addr &&
            addr <= comp_addr_range[i].end_addr) {
            return true;
       }
    }

    return false;
}

static void check_load_psw(SecureIplCompAddrRange *comp_addr_range,
                           int addr_range_index, uint64_t sclab_load_psw,
                           uint64_t load_psw, IplDeviceComponentList *comps,
                           int comp_index)
{
    uint32_t flag;
    const char *msg;
    bool valid;

    valid = is_psw_valid(sclab_load_psw, comp_addr_range, addr_range_index) &&
            is_psw_valid(load_psw, comp_addr_range, addr_range_index);
    if (!valid) {
        flag = S390_IPL_COMPONENT_CEI_INVALID_LOAD_PSW;
        msg = "Invalid PSW";
        set_cei_with_log(comps, comp_index, flag, msg);
    }

    valid = validate_lpsw(sclab_load_psw, load_psw);
    if (!valid) {
        flag = S390_IPL_COMPONENT_CEI_UNMATCHED_SCLAB_LOAD_PSW;
        msg = "Load PSW does not match with PSW in component";
        set_cei_with_log(comps, comp_index, flag, msg);
    }
}

static void check_nuc(uint16_t global_sclab_flags, int unsigned_count,
                      IplDeviceComponentList *comps)
{
    uint16_t flag;
    const char *msg;
    bool is_nuc_set;

    is_nuc_set = is_sclab_flag_set(global_sclab_flags, S390_SECURE_IPL_SCLAB_FLAG_NUC);
    if (is_nuc_set && unsigned_count > 0) {
        flag = S390_IPL_INFO_IIEI_FOUND_UNSIGNED_COMP;
        msg = "Unsigned components are not allowed";
        set_iiei_with_log(comps, flag, msg);
    }
}

static void check_sc(uint16_t global_sclab_flags, int signed_count,
                     IplDeviceComponentList *comps)
{
    uint16_t flag;
    const char *msg;
    bool is_sc_set;

    is_sc_set = is_sclab_flag_set(global_sclab_flags, S390_SECURE_IPL_SCLAB_FLAG_SC);
    if (is_sc_set && signed_count != 1) {
        flag = S390_IPL_INFO_IIEI_MORE_SIGNED_COMP;
        msg = "Only one signed component is allowed";
        set_iiei_with_log(comps, flag, msg);
    }
}

void check_global_sclab(SecureIplSclabInfo sclab_info,
                        SecureIplCompAddrRange *comp_addr_range,
                        int addr_range_index, uint64_t load_psw,
                        int unsigned_count, int signed_count,
                        IplDeviceComponentList *comps, int comp_index)
{
    uint16_t flag;
    const char *msg;

    if (sclab_info.count == 0) {
        return;
    }

    if (sclab_info.global_count == 0) {
        flag = S390_IPL_INFO_IIEI_NO_GLOBAL_SCLAB;
        msg = "Global SCLAB does not exists";
        set_iiei_with_log(comps, flag, msg);
        return;
    }

    if (sclab_info.global_count > 1) {
        flag = S390_IPL_INFO_IIEI_MORE_GLOBAL_SCLAB;
        msg = "More than one global SCLAB";
        set_iiei_with_log(comps, flag, msg);
        return;
    }

    if (sclab_info.load_psw) {
        /* Verify PSW from the final component entry with PSW from the global SCLAB */
        check_load_psw(comp_addr_range, addr_range_index,
                          sclab_info.load_psw, load_psw,
                          comps, comp_index);
    }

    if (sclab_info.flags) {
        /* Unsigned components are not allowed if NUC flag is set in the global SCLAB */
        check_nuc(sclab_info.flags, unsigned_count, comps);

        /* Only one signed component is allowed is SC flag is set in the global SCLAB */
        check_sc(sclab_info.flags, signed_count, comps);
    }
}

static void check_signed_comp(int signed_count, IplDeviceComponentList *comps)
{
    uint16_t flag;
    const char *msg;

    if (signed_count > 0) {
        return;
    }

    flag =  S390_IPL_INFO_IIEI_NO_SIGNED_COMP;
    msg = "Secure boot is on, but components are not signed";
    set_iiei_with_log(comps, flag, msg);
}

static void check_sclab_count(int count, IplDeviceComponentList *comps)
{
    uint16_t flag;
    const char *msg;

    if (count > 0) {
        return;
    }

    flag = S390_IPL_INFO_IIEI_NO_SCLAB;
    msg = "No recognizable SCLAB";
    set_iiei_with_log(comps, flag, msg);
}

static void check_unsigned_comp(uint64_t comp_addr, IplDeviceComponentList *comps,
                                int comp_index, int cert_index, uint64_t comp_len)
{
    check_unsigned_addr(comp_addr, comps, comp_index);

    comp_list_add(comps, comp_index, cert_index, comp_addr, comp_len, 0x00);
}

static void check_sclab(uint64_t comp_addr, IplDeviceComponentList *comps,
                        uint64_t comp_len, int comp_index, SecureIplSclabInfo *sclab_info)
{
    SclabOriginLocator *sclab_locator;
    SecureCodeLoadingAttributesBlock *sclab;
    bool exist;
    bool valid;

    sclab_locator = (SclabOriginLocator *)(comp_addr + comp_len - 8);

    /* return early if sclab does not exist */
    exist = check_sclab_presence(sclab_locator->magic, comps, comp_index);
    if (!exist) {
        return;
    }

    check_sclab_length(sclab_locator->len, comps, comp_index);

    /* return early if sclab is invalid */
    valid = (comps->device_entries[comp_index].cei &
             S390_IPL_COMPONENT_CEI_INVALID_SCLAB) == 0;
    if (!valid) {
        return;
    }

    sclab_info->count += 1;
    sclab = (SecureCodeLoadingAttributesBlock *)(comp_addr + comp_len -
                                                    sclab_locator->len);

    check_sclab_format(sclab->format, comps, comp_index);
    check_sclab_opsw(sclab, sclab_info, comps, comp_index);
    check_sclab_ola(sclab, comp_addr, comps, comp_index);
    check_sclab_nuc(sclab->flags, comps, comp_index);
    check_sclab_sc(sclab->flags, comps, comp_index);
}

static uint32_t zipl_load_signature(ComponentEntry *entry, uint64_t sig_sec)
{
    uint32_t sig_len;

    if (zipl_load_segment(entry, sig_sec) < 0) {
        return -1;
    }

    if (entry->compdat.sig_info.format != DER_SIGNATURE_FORMAT) {
        puts("Signature is not in DER format");
        return -1;
    }
    sig_len = entry->compdat.sig_info.sig_len;

    return sig_len;
}

static int handle_certificate(int *cert_table, uint8_t **cert,
                             uint64_t cert_len, uint8_t cert_idx,
                             IplSignatureCertificateList *certs, int cert_index)
{
    bool unused;

    unused = cert_table[cert_idx] == -1;
    if (unused) {
        if (request_certificate(*cert, cert_idx)) {
            cert_list_add(certs, cert_index, *cert, cert_len);
            cert_table[cert_idx] = cert_index;
            *cert += cert_len;
        } else {
            puts("Could not get certificate");
            return -1;
        }

        /* increment cert_index for the next cert entry */
        return ++cert_index;
    }

    return cert_index;
}

int zipl_run_secure(ComponentEntry **entry_ptr, uint8_t *tmp_sec)
{
    IplDeviceComponentList comps;
    IplSignatureCertificateList certs;
    ComponentEntry *entry = *entry_ptr;
    uint8_t *cert = NULL;
    uint64_t *sig = NULL;
    int cert_index = 0;
    int comp_index = 0;
    uint64_t comp_addr;
    int comp_len;
    uint32_t sig_len = 0;
    uint64_t cert_len = -1;
    uint8_t cert_idx = -1;
    bool verified;
    uint32_t certs_len;
    /*
     * Store indices of cert entry that have already used for signature verification
     * to prevent allocating the same certificate multiple times.
     * cert_table index: index of certificate from qemu cert store used for verification
     * cert_table value: index of cert entry in cert list that contains the certificate
     */
    int cert_table[MAX_CERTIFICATES] = { [0 ... MAX_CERTIFICATES - 1] = -1};
    SecureIplCompAddrRange comp_addr_range[MAX_CERTIFICATES];
    int addr_range_index = 0;
    int signed_count = 0;
    int unsigned_count = 0;
    SecureIplSclabInfo sclab_info = { 0 };

    if (!secure_ipl_supported()) {
        return -1;
    }

    init_lists(&comps, &certs);
    certs_len = get_certs_length();
    cert = malloc(certs_len);
    sig = malloc(MAX_SECTOR_SIZE);

    while (entry->component_type != ZIPL_COMP_ENTRY_EXEC) {
        switch (entry->component_type) {
        case ZIPL_COMP_ENTRY_SIGNATURE:
            if (sig_len) {
                goto out;
            }

            sig_len = zipl_load_signature(entry, (uint64_t)sig);
            if (sig_len < 0) {
                goto out;
            }
            break;
        case ZIPL_COMP_ENTRY_LOAD:
            comp_addr = entry->compdat.load_addr;
            comp_len = zipl_load_segment(entry, comp_addr);
            if (comp_len < 0) {
                goto out;
            }

            addr_overlap_check(comp_addr_range, &addr_range_index,
                               comp_addr, comp_addr + comp_len, sig_len > 0);

            if (!sig_len) {
                check_unsigned_comp(comp_addr, &comps, comp_index, cert_index, comp_len);
                unsigned_count += 1;
                comp_index++;
                break;
            }

            check_sclab(comp_addr, &comps, comp_len, comp_index, &sclab_info);
            verified = verify_signature(comp_len, comp_addr, sig_len, (uint64_t)sig,
                                        &cert_len, &cert_idx);

            if (verified) {
                cert_index = handle_certificate(cert_table, &cert, cert_len, cert_idx,
                                                &certs, cert_index);
                if (cert_index == -1) {
                    goto out;
                }

                puts("Verified component");
                comp_list_add(&comps, comp_index, cert_table[cert_idx],
                              comp_addr, comp_len,
                              S390_IPL_COMPONENT_FLAG_SC | S390_IPL_COMPONENT_FLAG_CSV);
            } else {
                comp_list_add(&comps, comp_index, -1,
                              comp_addr, comp_len,
                              S390_IPL_COMPONENT_FLAG_SC);
                zipl_secure_handle("Could not verify component");
            }

            comp_index++;
            signed_count += 1;
            /* After a signature is used another new one can be accepted */
            sig_len = 0;
            break;
        default:
            puts("Unknown component entry type");
            return -1;
        }

        entry++;

        if ((uint8_t *)(&entry[1]) > tmp_sec + MAX_SECTOR_SIZE) {
            puts("Wrong entry value");
            return -EINVAL;
        }
    }

    check_signed_comp(signed_count, &comps);
    check_sclab_count(sclab_info.count, &comps);
    check_global_sclab(sclab_info, comp_addr_range, addr_range_index,
                       entry->compdat.load_psw, unsigned_count, signed_count,
                       &comps, comp_index);

    if (update_iirb(&comps, &certs)) {
        zipl_secure_handle("Failed to write IPL Information Report Block");
    }

    *entry_ptr = entry;
    free(sig);

    return 0;
out:
    free(cert);
    free(sig);

    return -1;
}
