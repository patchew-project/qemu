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

static uint8_t vcb_data[MAX_SECTOR_SIZE * 4] __attribute__((__aligned__(PAGE_SIZE)));
static uint8_t vcssb_data[VCSSB_MIN_LEN] __attribute__((__aligned__(8)));

VCStorageSizeBlock *zipl_secure_get_vcssb(void)
{
    VCStorageSizeBlock *vcssb;

    vcssb = (VCStorageSizeBlock *)vcssb_data;
    /* avoid retrieving vcssb multiple times */
    if (vcssb->length >= VCSSB_MIN_LEN) {
        return vcssb;
    }

    if (!is_cert_store_facility_supported()) {
        puts("Certificate Store Facility is not supported by the hypervisor!");
        return NULL;
    }

    vcssb->length = VCSSB_MIN_LEN;
    if (diag320(vcssb, DIAG_320_SUBC_QUERY_VCSI) != DIAG_320_RC_OK) {
        vcssb->length = 0;
        return NULL;
    }

    return vcssb;
}

static uint32_t get_total_certs_length(void)
{
    VCStorageSizeBlock *vcssb;

    vcssb = zipl_secure_get_vcssb();
    if (vcssb == NULL) {
        return 0;
    }

    return vcssb->total_vcb_len - VCB_HEADER_LEN - vcssb->total_vc_ct * VCE_HEADER_LEN;
}

static uint32_t request_certificate(uint8_t *cert_addr, uint8_t index)
{
    VCStorageSizeBlock *vcssb;
    VCBlock *vcb;
    VCEntry *vce;
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
    vcb = (VCBlock *)vcb_data;
    vcb->in_len = ROUND_UP(vcssb->max_single_vcb_len, PAGE_SIZE);
    vcb->first_vc_index = index;
    vcb->last_vc_index = index;

    if (diag320(vcb, DIAG_320_SUBC_STORE_VC) != DIAG_320_RC_OK) {
        goto out;
    }

    if (vcb->out_len == VCB_HEADER_LEN) {
        puts("No certificate entry");
        goto out;
    }

    if (vcb->remain_ct != 0) {
        puts("Not enough memory to store all requested certificates");
        goto out;
    }

    vce = (VCEntry *)vcb->vce_buf;
    if (!(vce->flags & DIAG_320_VCE_FLAGS_VALID)) {
        puts("Invalid certificate");
        goto out;
    }

    cert_len = vce->cert_len;
    memcpy(cert_addr, (uint8_t *)vce + vce->cert_offset, vce->cert_len);

out:
    memset(vcb_data, 0, sizeof(vcb_data));
    return cert_len;
}

static void cert_list_add(IplSignatureCertificateList *cert_list, int cert_entry_idx,
                          uint8_t *cert_addr, uint64_t cert_len)
{
    if (cert_entry_idx > MAX_CERTIFICATES - 1) {
        printf("Warning: Ignoring cert entry #%d because only %d entries are supported\n",
                cert_entry_idx + 1, MAX_CERTIFICATES);
        return;
    }

    cert_list->cert_entries[cert_entry_idx].addr = (uint64_t)cert_addr;
    cert_list->cert_entries[cert_entry_idx].len = cert_len;
    cert_list->ipl_info_header.len += sizeof(cert_list->cert_entries[cert_entry_idx]);
}

static void comp_list_add(IplDeviceComponentList *comp_list, int comp_entry_idx,
                          SecureIplCompEntryInfo comp_entry_info)
{
    if (comp_entry_idx > MAX_CERTIFICATES - 1) {
        printf("Warning: Ignoring comp entry #%d because only %d entries are supported\n",
                comp_entry_idx + 1, MAX_CERTIFICATES);
        return;
    }

    comp_list->device_entries[comp_entry_idx].addr = comp_entry_info.addr;
    comp_list->device_entries[comp_entry_idx].len = comp_entry_info.len;
    comp_list->device_entries[comp_entry_idx].cei = comp_entry_info.cei;
    comp_list->device_entries[comp_entry_idx].flags = comp_entry_info.flags;
    /* cert index field is meaningful only when S390_IPL_DEV_COMP_FLAG_SC is set */
    comp_list->device_entries[comp_entry_idx].cert_index = comp_entry_info.cert_index;
    comp_list->ipl_info_header.len += sizeof(comp_list->device_entries[comp_entry_idx]);
}

static void update_iirb(IplDeviceComponentList *comp_list,
                        IplSignatureCertificateList *cert_list)
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
    comps_len = comp_list->ipl_info_header.len;
    certs_len = cert_list->ipl_info_header.len;
    if ((comps_len + certs_len + iirb_hdr_len) > sizeof(IplInfoReportBlock)) {
        panic("Not enough space to hold all components and certificates in IIRB");
    }

    /* IIRB immediately follows IPLB */
    iirb = &ipl_data.iirb;
    iirb->hdr.len = iirb_hdr_len;

    /* Copy IPL device component list after IIRB Header */
    iirb_comps = (IplDeviceComponentList *) iirb->info_blks;
    memcpy(iirb_comps, comp_list, comps_len);

    /* Update IIRB length */
    iirb->hdr.len += comps_len;

    /* Copy IPL sig cert list after IPL device component list */
    iirb_certs = (IplSignatureCertificateList *) (iirb->info_blks +
                                                  iirb_comps->ipl_info_header.len);
    memcpy(iirb_certs, cert_list, certs_len);

    /* Update IIRB length */
    iirb->hdr.len += certs_len;
}

static bool secure_ipl_supported(void)
{
    if (!sclp_is_sipl_on()) {
        puts("Secure IPL Facility is not supported by the hypervisor!");
        return false;
    }

    if (!is_signature_verif_supported()) {
        puts("Secure IPL extensions are not supported by the hypervisor!");
        return false;
    }

    if (!is_cert_store_facility_supported()) {
        puts("Certificate Store Facility is not supported by the hypervisor!");
        return false;
    }

    if (!sclp_is_sclaf_on()) {
        puts("Secure IPL Code Loading Attributes Facility is not supported by"
             " the hypervisor!");
        return false;
    }

    return true;
}

static void init_lists(IplDeviceComponentList *comp_list,
                       IplSignatureCertificateList *cert_list)
{
    comp_list->ipl_info_header.type = IPL_INFO_BLOCK_TYPE_COMPONENTS;
    comp_list->ipl_info_header.len = sizeof(comp_list->ipl_info_header);

    cert_list->ipl_info_header.type = IPL_INFO_BLOCK_TYPE_CERTIFICATES;
    cert_list->ipl_info_header.len = sizeof(cert_list->ipl_info_header);
}

static bool is_comp_overlap(SecureIplCompAddrRangeList *range_list,
                            SecureIplCompEntryInfo comp_entry_info)
{
    uint64_t start_addr;
    uint64_t end_addr;

    start_addr = comp_entry_info.addr;
    end_addr = comp_entry_info.addr + comp_entry_info.len;

    /*
     * Check component's address range does not overlap with any
     * signed component's address range.
     */
    for (int i = 0; i < range_list->index; i++) {
        if ((range_list->comp_addr_range[i].start_addr < end_addr &&
            start_addr < range_list->comp_addr_range[i].end_addr) &&
            range_list->comp_addr_range[i].is_signed) {
            return true;
       }
    }
    return false;
}

static void comp_addr_range_add(SecureIplCompAddrRangeList *range_list,
                                SecureIplCompEntryInfo comp_entry_info,
                                bool is_signed)
{
    uint64_t start_addr;
    uint64_t end_addr;

    start_addr = comp_entry_info.addr;
    end_addr = comp_entry_info.addr + comp_entry_info.len;

    if (range_list->index >= MAX_CERTIFICATES) {
        zipl_secure_handle("Component address range update failed due to out-of-range"
                           " index; Overlapping validation cannot be guaranteed");
    }

    range_list->comp_addr_range[range_list->index].is_signed = is_signed;
    range_list->comp_addr_range[range_list->index].start_addr = start_addr;
    range_list->comp_addr_range[range_list->index].end_addr = end_addr;
}

static void check_unsigned_addr(SecureIplCompEntryInfo *comp_entry_info)
{
    /* unsigned load address must be greater than or equal to 0x2000 */
    comp_entry_info->cei |= validate_comp_condition(
                    comp_entry_info->addr >= S390_SECURE_IPL_UNSIGNED_MIN_ADDR,
                    S390_CEI_INVALID_UNSIGNED_ADDR,
                    "Load address is less than 0x2000");
}

static bool check_sclab_presence(uint8_t *sclab_magic, uint32_t *cei_flags)
{
    /* identifies the presence of SCLAB */
    if (magic_match(sclab_magic, ZIPL_MAGIC)) {
        return true;
    }

    *cei_flags |= S390_CEI_INVALID_SCLAB;

    /* a missing SCLAB will not be reported in audit mode */
    return false;
}

static void check_sclab_length(uint16_t sclab_len, uint32_t *cei_flags)
{
    *cei_flags |= validate_comp_condition(sclab_len >= S390_SECURE_IPL_SCLAB_MIN_LEN,
                                          S390_CEI_INVALID_SCLAB_LEN |
                                          S390_CEI_INVALID_SCLAB,
                                          "Invalid SCLAB length");
}

static void check_sclab_format(uint8_t sclab_format, uint32_t *cei_flags)
{
    /* SCLAB format must set to zero, indicating a format-0 SCLAB being used */
    *cei_flags |= validate_comp_condition(sclab_format == 0,
                                          S390_CEI_INVALID_SCLAB_FORMAT,
                                          "Format-0 SCLAB is not being used");
}

static void check_sclab_opsw(SecureCodeLoadingAttributesBlock *sclab,
                             SecureIplSclabInfo *sclab_info, uint32_t *cei_flags)
{
    const char *msg;

    if (!(sclab->flags & S390_SECURE_IPL_SCLAB_FLAG_OPSW)) {
        /* OPSW = 0 - Load PSW field in SCLAB must contain zeros */
        msg = "Load PSW is not zero when Override PSW bit is zero";
        *cei_flags |= validate_comp_condition(sclab->load_psw == 0,
                                              S390_CEI_SCLAB_LOAD_PSW_NOT_ZERO,
                                              msg);

    } else {
        /* OPSW = 1 indicating global SCLAB */
        sclab_info->global_count += 1;
        if (sclab_info->global_count == 1) {
            sclab_info->global_load_psw = sclab->load_psw;
            sclab_info->global_flags = sclab->flags;
        }

        /* OLA must set to one */
        msg = "Override Load Address bit is not set to one in the global SCLAB";
        *cei_flags |= validate_comp_condition(
                            sclab->flags & S390_SECURE_IPL_SCLAB_FLAG_OLA,
                            S390_CEI_SCLAB_OLA_NOT_ONE, msg);
    }
}

static void check_sclab_ola(SecureCodeLoadingAttributesBlock *sclab,
                            uint64_t load_addr, uint32_t *cei_flags)
{
    const char *msg;

    if (!(sclab->flags & S390_SECURE_IPL_SCLAB_FLAG_OLA)) {
        /* OLA = 0 - Load address field in SCLAB must contain zeros */
        msg = "Load Address is not zero when Override Load Address bit is zero";
        *cei_flags |= validate_comp_condition(sclab->load_addr == 0,
                                              S390_CEI_SCLAB_LOAD_ADDR_NOT_ZERO,
                                              msg);
    } else {
        /* OLA = 1 - Load address field must match storage address of the component */
        msg = "Load Address does not match with component load address";
        *cei_flags |= validate_comp_condition(sclab->load_addr == load_addr,
                                              S390_CEI_UNMATCHED_SCLAB_LOAD_ADDR,
                                              msg);
    }
}

static void check_sclab_nuc(uint16_t sclab_flags, uint32_t *cei_flags)
{
    const char *msg;
    bool is_nuc_set;
    bool is_global_sclab;

    is_nuc_set = sclab_flags & S390_SECURE_IPL_SCLAB_FLAG_NUC;
    is_global_sclab = sclab_flags & S390_SECURE_IPL_SCLAB_FLAG_OPSW;
    msg = "No Unsigned Components bit is set, but not in the global SCLAB";
    *cei_flags |= validate_comp_condition(!is_nuc_set || is_global_sclab,
                                          S390_CEI_NUC_NOT_IN_GLOBAL_SCLA, msg);
}

static void check_sclab_sc(uint16_t sclab_flags, uint32_t *cei_flags)
{
    const char *msg;
    bool is_sc_set;
    bool is_global_sclab;

    is_sc_set = sclab_flags & S390_SECURE_IPL_SCLAB_FLAG_SC;
    is_global_sclab = sclab_flags & S390_SECURE_IPL_SCLAB_FLAG_OPSW;
    msg = "Single Component bit is set, but not in the global SCLAB";
    *cei_flags |= validate_comp_condition(!is_sc_set || is_global_sclab,
                                          S390_CEI_SC_NOT_IN_GLOBAL_SCLAB, msg);
}

static bool is_psw_valid(uint64_t psw, SecureIplCompAddrRangeList *range_list)
{
    uint32_t addr = psw & 0x7fffffff;

    /* PSW points within a signed binary code component */
    for (int i = 0; i < range_list->index; i++) {
        if (range_list->comp_addr_range[i].is_signed &&
            addr >= range_list->comp_addr_range[i].start_addr &&
            addr <= range_list->comp_addr_range[i].end_addr - 2) {
            return true;
       }
    }
    return false;
}

static void check_load_psw(SecureIplCompAddrRangeList *range_list,
                           uint64_t sclab_load_psw,
                           SecureIplCompEntryInfo *comp_entry_info)
{
    bool valid;
    uint64_t load_psw;

    load_psw = comp_entry_info->addr;
    valid = is_psw_valid(sclab_load_psw, range_list) &&
            is_psw_valid(load_psw, range_list);
    comp_entry_info->cei |= validate_comp_condition(valid,
                                                    S390_CEI_INVALID_LOAD_PSW,
                                                    "Invalid PSW");

    /* compare load PSW with the PSW specified in component */
    comp_entry_info->cei |= validate_comp_condition(sclab_load_psw == load_psw,
                                          S390_CEI_UNMATCHED_SCLAB_LOAD_PSW,
                                         "Load PSW does not match with PSW in component");
}

static void check_no_unsigned_comp(SecureIplSclabInfo sclab_info,
                                   IplDeviceComponentList *comp_list)
{
    bool is_nuc_set;

    is_nuc_set = sclab_info.global_flags & S390_SECURE_IPL_SCLAB_FLAG_NUC;
    if (is_nuc_set && sclab_info.unsigned_count > 0) {
        comp_list->ipl_info_header.iiei |= S390_IIEI_FOUND_UNSIGNED_COMP;
        zipl_secure_handle("Unsigned components are not allowed");
    }
}

static void check_single_comp(SecureIplSclabInfo sclab_info,
                              IplDeviceComponentList *comp_list)
{
    bool is_sc_set;

    is_sc_set = sclab_info.global_flags & S390_SECURE_IPL_SCLAB_FLAG_SC;
    if (is_sc_set &&
        sclab_info.signed_count != 1 &&
        sclab_info.unsigned_count >= 0) {
        comp_list->ipl_info_header.iiei |= S390_IIEI_MORE_SIGNED_COMP;
        zipl_secure_handle("Only one signed component is allowed");
    }
}

void check_global_sclab(SecureIplSclabInfo sclab_info,
                        IplDeviceComponentList *comp_list)
{
    if (sclab_info.count == 0) {
        return;
    }

    if (sclab_info.global_count == 0) {
        comp_list->ipl_info_header.iiei |= S390_IIEI_NO_GLOBAL_SCLAB;
        zipl_secure_handle("Global SCLAB does not exists");
        return;
    }

    if (sclab_info.global_count > 1) {
        comp_list->ipl_info_header.iiei |= S390_IIEI_MORE_GLOBAL_SCLAB;
        zipl_secure_handle("More than one global SCLAB");
        return;
    }

    if (sclab_info.global_flags) {
        /* Unsigned components are not allowed if NUC flag is set in the global SCLAB */
        check_no_unsigned_comp(sclab_info, comp_list);

        /* Only one signed component is allowed is SC flag is set in the global SCLAB */
        check_single_comp(sclab_info, comp_list);
    }
}

static void check_has_signed_comp(int signed_count, IplDeviceComponentList *comp_list)
{
    const char *msg;

    msg = "Secure boot is on, but components are not signed";
    comp_list->ipl_info_header.iiei |=
                validate_comp_condition(signed_count > 0,
                                        S390_IIEI_NO_SIGNED_COMP, msg);

}

static void check_sclab_count(int count, IplDeviceComponentList *comp_list)
{
    comp_list->ipl_info_header.iiei |=
                validate_comp_condition(count > 0, S390_IIEI_NO_SCLAB,
                                        "No recognizable SCLAB");
}

static void check_sclab(SecureIplCompEntryInfo *comp_entry_info,
                        SecureIplSclabInfo *sclab_info)
{
    SclabOriginLocator *sclab_locator;
    SecureCodeLoadingAttributesBlock *sclab;

    /* sclab locator is located at the last 8 bytes of the signed comp */
    sclab_locator = (SclabOriginLocator *)(comp_entry_info->addr +
                                           comp_entry_info->len - 8);

    /* return early if sclab does not exist */
    if (!check_sclab_presence(sclab_locator->magic, &comp_entry_info->cei)) {
        return;
    }

    check_sclab_length(sclab_locator->len, &comp_entry_info->cei);

    /* return early if sclab is invalid */
    if (comp_entry_info->cei & S390_CEI_INVALID_SCLAB) {
        return;
    }

    sclab_info->count += 1;
    sclab = (SecureCodeLoadingAttributesBlock *)(comp_entry_info->addr +
                                                 comp_entry_info->len -
                                                 sclab_locator->len);

    check_sclab_format(sclab->format, &comp_entry_info->cei);
    check_sclab_opsw(sclab, sclab_info, &comp_entry_info->cei);
    check_sclab_ola(sclab, comp_entry_info->addr, &comp_entry_info->cei);
    check_sclab_nuc(sclab->flags, &comp_entry_info->cei);
    check_sclab_sc(sclab->flags, &comp_entry_info->cei);
}

static int zipl_load_signature(ComponentEntry *entry, uint64_t sig_sec)
{
    if (zipl_load_segment(entry, sig_sec) < 0) {
        return -1;
    }

    if (entry->compdat.sig_info.format != DER_SIGNATURE_FORMAT) {
        puts("Signature is not in DER format");
        return -1;
    }

    return entry->compdat.sig_info.sig_len;
}

int zipl_run_secure(ComponentEntry **entry_ptr, uint8_t *tmp_sec)
{
    IplDeviceComponentList comp_list = { 0 };
    IplSignatureCertificateList cert_list = { 0 };
    SecureIplCompEntryInfo comp_entry_info;
    ComponentEntry *entry = *entry_ptr;
    uint8_t *cert_addr = NULL;
    uint64_t *sig = NULL;
    int cert_entry_idx = 0;
    int comp_entry_idx = 0;
    int sig_len = 0;
    int comp_len;
    uint64_t comp_addr;
    uint64_t cert_len;
    uint8_t cert_table_idx;
    bool verified;
    /*
     * Keep track of which certificate store indices correspond to the
     * certificate data entries within the IplSignatureCertificateList to
     * prevent allocating space for the same certificate multiple times.
     *
     * The array index corresponds to the certificate's cert-store index.
     *
     * The array value corresponds to the certificate's entry within the
     * IplSignatureCertificateList (with a value of -1 denoting no entry
     * exists for the certificate).
     */
    int cert_list_table[MAX_CERTIFICATES] = { [0 ... MAX_CERTIFICATES - 1] = -1 };
    SecureIplCompAddrRangeList range_list = { 0 };
    SecureIplSclabInfo sclab_info = { 0 };

    if (!secure_ipl_supported()) {
        panic("Unable to boot in secure/audit mode");
    }

    init_lists(&comp_list, &cert_list);
    cert_addr = malloc(get_total_certs_length());
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

            comp_entry_info = (SecureIplCompEntryInfo){ 0 };
            comp_entry_info.addr = comp_addr;
            comp_entry_info.len = (uint64_t)comp_len;

            if (is_comp_overlap(&range_list, comp_entry_info)) {
                zipl_secure_handle("Component addresses overlap");
            }
            comp_addr_range_add(&range_list, comp_entry_info, !!sig_len);
            range_list.index += 1;

            if (!sig_len) {
                check_unsigned_addr(&comp_entry_info);
                comp_list_add(&comp_list, comp_entry_idx, comp_entry_info);

                sclab_info.unsigned_count += 1;
                comp_entry_idx++;
                break;
            }

            check_sclab(&comp_entry_info, &sclab_info);
            verified = verify_signature(comp_entry_info,
                                        sig_len, (uint64_t)sig,
                                        &cert_len, &cert_table_idx);

            /* default flags for unverified component */
            comp_entry_info.flags |= S390_IPL_DEV_COMP_FLAG_SC;

            if (verified) {
                if (cert_list_table[cert_table_idx] == -1) {
                    if (!request_certificate(cert_addr, cert_table_idx)) {
                        puts("Could not get certificate");
                        goto out;
                    }

                    cert_list_table[cert_table_idx] = cert_entry_idx;
                    cert_list_add(&cert_list, cert_entry_idx, cert_addr, cert_len);

                    /* increment for the next certificate */
                    cert_entry_idx++;
                    cert_addr += cert_len;
                }

                puts("Verified component");
                comp_entry_info.cert_index = cert_list_table[cert_table_idx];
                comp_entry_info.flags |= S390_IPL_DEV_COMP_FLAG_CSV;
            }

            comp_list_add(&comp_list, comp_entry_idx, comp_entry_info);

            if (!verified) {
                zipl_secure_handle("Could not verify component");
            }

            comp_entry_idx++;
            sclab_info.signed_count += 1;
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

    /* validate load PSW with PSW specified in the final entry */
    if (sclab_info.global_load_psw) {
        comp_entry_info = (SecureIplCompEntryInfo){ 0 };
        comp_entry_info.addr = entry->compdat.load_psw;

        check_load_psw(&range_list, sclab_info.global_load_psw, &comp_entry_info);
        comp_list_add(&comp_list, comp_entry_idx, comp_entry_info);
    }

    check_has_signed_comp(sclab_info.signed_count, &comp_list);
    check_sclab_count(sclab_info.count, &comp_list);
    check_global_sclab(sclab_info, &comp_list);

    update_iirb(&comp_list, &cert_list);

    *entry_ptr = entry;
    free(sig);

    return 0;
out:
    free(cert_addr);
    free(sig);

    return -1;
}
