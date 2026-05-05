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
#include "sclp.h"
#include "secure-ipl.h"

static VCStorageSizeBlock vcssb __attribute__((__aligned__(8)));

VCStorageSizeBlock *zipl_secure_get_vcssb(void)
{
    /* avoid retrieving vcssb multiple times */
    if (vcssb.length == VCSSB_MIN_LEN) {
        return &vcssb;
    }

    vcssb.length = VCSSB_MIN_LEN;
    if (_diag320(&vcssb, DIAG_320_SUBC_QUERY_VCSI) != DIAG_320_RC_OK) {
        vcssb.length = 0;
        return NULL;
    }

    return &vcssb;
}

static uint32_t get_total_certs_length(void)
{
    if (zipl_secure_get_vcssb() == NULL) {
        return 0;
    }

    return vcssb.total_vcb_len - sizeof(VCBlockHeader) -
           vcssb.total_vc_ct * sizeof(VCEntryHeader);
}

static uint32_t request_certificate(uint8_t *cert_buf, uint8_t index)
{
    VCEntryHeader *vce_hdr;
    struct vcb {
        VCBlockHeader vcb_hdr;
        struct vce {
            VCEntryHeader vce_hdr;
            uint8_t cert_buf[CERT_BUF_MAX_LEN];
        } vce;
    } __attribute__((__aligned__(4096))) vcb = { 0 };

    /* Get Verification Certificate Storage Size block with DIAG320 subcode 1 */
    if (zipl_secure_get_vcssb() == NULL) {
        return 0;
    }

    /*
     * Request single entry
     * Fill input fields of single-entry VCB
     *
     * First and last index must be equal because only one
     * VCE per VCB is currently supported
     */
    vcb.vcb_hdr.in_len = ROUND_UP(vcssb.max_single_vcb_len, PAGE_SIZE);
    vcb.vcb_hdr.first_vc_index = index;
    vcb.vcb_hdr.last_vc_index = index;

    if (_diag320(&vcb, DIAG_320_SUBC_STORE_VC) != DIAG_320_RC_OK) {
        return 0;
    }

    if (vcb.vcb_hdr.out_len == sizeof(VCBlockHeader)) {
        puts("No certificate entry");
        return 0;
    }

    if (vcb.vcb_hdr.remain_ct != 0) {
        panic("Not enough memory to store all requested certificates");
    }

    vce_hdr = &vcb.vce.vce_hdr;
    if (!(vce_hdr->flags & DIAG_320_VCE_FLAGS_VALID)) {
        puts("Invalid certificate");
        return 0;
    }

    memcpy(cert_buf, (uint8_t *)&vcb.vce + vce_hdr->cert_offset, vce_hdr->cert_len);

    return vce_hdr->cert_len;
}

static int cert_list_add(IplSignatureCertificateList *cert_list,
                         uint8_t *cert_buf, uint64_t cert_len)
{
    static bool warned;
    int cert_entry_idx;

    cert_entry_idx = (cert_list->ipl_info_header.len - sizeof(IplInfoBlockHeader)) /
                     sizeof(IplSignatureCertificateEntry);
    if (cert_entry_idx > MAX_CERTIFICATES - 1) {
        if (!warned) {
            printf("Warning: only %d cert entries are supported;"
                   " additional entries are ignored\n",
                   MAX_CERTIFICATES);
            warned = true;
        }
        return cert_entry_idx;
    }

    cert_list->cert_entries[cert_entry_idx].addr = (uint64_t)cert_buf;
    cert_list->cert_entries[cert_entry_idx].len = cert_len;
    cert_list->ipl_info_header.len += sizeof(IplSignatureCertificateEntry);

    return cert_entry_idx;
}

static void comp_list_add(IplDeviceComponentList *comp_list,
                          SecureIplCompEntryInfo comp_entry_info)
{
    int comp_entry_idx;

    comp_entry_idx = (comp_list->ipl_info_header.len - sizeof(IplInfoBlockHeader)) /
                     sizeof(IplDeviceComponentEntry);
    if (comp_entry_idx > MAX_COMP_ENTRIES - 1) {
        printf("Warning: only %d component entries are supported\n",
                MAX_COMP_ENTRIES);
        panic("The device component list has reached its maximum capacity");
    }

    comp_list->device_entries[comp_entry_idx].addr = comp_entry_info.addr;
    comp_list->device_entries[comp_entry_idx].len = comp_entry_info.len;
    comp_list->device_entries[comp_entry_idx].cei = comp_entry_info.cei;
    comp_list->device_entries[comp_entry_idx].flags = comp_entry_info.flags;
    /* cert index field is meaningful only when S390_IPL_DEV_COMP_FLAG_SC is set */
    if (comp_entry_info.flags & S390_IPL_DEV_COMP_FLAG_SC) {
        comp_list->device_entries[comp_entry_idx].cert_index =
                                                  comp_entry_info.cert_index;
    }
    comp_list->ipl_info_header.len += sizeof(IplDeviceComponentEntry);
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

bool secure_ipl_supported(void)
{
    if (!sclp_is_fac_ipl_flag_on(SCCB_FAC_IPL_SIPL_BIT)) {
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

    if (!sclp_is_fac_ipl_flag_on(SCCB_FAC_IPL_SCLAF_BIT)) {
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
    comp_list->ipl_info_header.len = sizeof(IplInfoBlockHeader);

    cert_list->ipl_info_header.type = IPL_INFO_BLOCK_TYPE_CERTIFICATES;
    cert_list->ipl_info_header.len = sizeof(IplInfoBlockHeader);
}

static void check_comp_overlap(SecureIplCompAddrRangeList *range_list,
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
    for (int i = 0; i < range_list->num; i++) {
        if (range_list->comp_addr_range[i].is_signed &&
            (range_list->comp_addr_range[i].start_addr < end_addr &&
            start_addr < range_list->comp_addr_range[i].end_addr)) {
            zipl_secure_error("Component addresses overlap");
       }
    }
}

static void comp_addr_range_add(SecureIplCompAddrRangeList *range_list,
                                SecureIplCompEntryInfo comp_entry_info,
                                bool is_signed)
{
    uint64_t start_addr;
    uint64_t end_addr;

    start_addr = comp_entry_info.addr;
    end_addr = comp_entry_info.addr + comp_entry_info.len;

    if (range_list->num >= MAX_COMP_ENTRIES) {
        zipl_secure_error("Component address range update failed due to out-of-range"
                          " index; Overlapping validation cannot be guaranteed");
        return;
    }

    range_list->comp_addr_range[range_list->num].is_signed = is_signed;
    range_list->comp_addr_range[range_list->num].start_addr = start_addr;
    range_list->comp_addr_range[range_list->num].end_addr = end_addr;

    range_list->num += 1;
}

static void check_sclab_opsw(SclaBlock *sclab, SecureIplSclabInfo *sclab_info,
                             uint32_t *cei_flags)
{
    if (!(sclab->flags & S390_SCLAB_OPSW)) {
        /* OPSW = 0 - Load PSW field in SCLAB must contain zeros */
        zipl_secure_validate(sclab->load_psw == 0, cei_flags,
                             S390_CEI_SCLAB_LOAD_PSW_NOT_ZERO,
                             "Load PSW is not zero when Override PSW bit is zero");
    } else {
        /* OPSW = 1 indicating global SCLAB */
        sclab_info->global_count += 1;
        if (sclab_info->global_count == 1) {
            sclab_info->global_load_psw = sclab->load_psw;
            sclab_info->global_flags = sclab->flags;
        }

        /* override load address flag must set to one */
        zipl_secure_validate(sclab->flags & S390_SCLAB_OLA, cei_flags,
                             S390_CEI_SCLAB_LOAD_PSW_NOT_ZERO,
                             "OLA flag is not set to one in the global SCLAB");
    }
}

static void check_sclab_ola(SclaBlock *sclab, uint64_t load_addr, uint32_t *cei_flags)
{
    if (!(sclab->flags & S390_SCLAB_OLA)) {
        /* OLA = 0 - Load address field in SCLAB must contain zeros */
        zipl_secure_validate(sclab->load_addr == 0, cei_flags,
                             S390_CEI_SCLAB_LOAD_ADDR_NOT_ZERO,
                             "Load Address is not zero when OLA flag is zero");
    } else {
        /* OLA = 1 - Load address field must match storage address of the component */
        zipl_secure_validate(sclab->load_addr == load_addr, cei_flags,
                             S390_CEI_UNMATCHED_SCLAB_LOAD_ADDR,
                             "Load Address does not match with component load address");
    }
}

static bool is_psw_valid(uint64_t psw, SecureIplCompAddrRangeList *range_list)
{
    uint32_t addr = psw & 0x7fffffff;

    /* PSW points within a signed binary code component */
    for (int i = 0; i < range_list->num; i++) {
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
    uint64_t load_psw;

    load_psw = comp_entry_info->addr;
    zipl_secure_validate(is_psw_valid(sclab_load_psw, range_list) &&
                         is_psw_valid(load_psw, range_list),
                         &comp_entry_info->cei, S390_CEI_INVALID_LOAD_PSW, "Invalid PSW");

    /* compare load PSW with the PSW specified in component */
    zipl_secure_validate(sclab_load_psw == load_psw, &comp_entry_info->cei,
                         S390_CEI_UNMATCHED_SCLAB_LOAD_PSW,
                         "Load PSW does not match with PSW in component");
}

static void check_no_unsigned_comp(SecureIplSclabInfo sclab_info,
                                   IplDeviceComponentList *comp_list)
{
    bool is_nuc_set;

    is_nuc_set = sclab_info.global_flags & S390_SCLAB_NUC;
    if (is_nuc_set && sclab_info.unsigned_count > 0) {
        comp_list->ipl_info_header.iiei |= S390_IIEI_FOUND_UNSIGNED_COMP;
        zipl_secure_error("Unsigned components are not allowed");
    }
}

static void check_single_comp(SecureIplSclabInfo sclab_info,
                              IplDeviceComponentList *comp_list)
{
    bool is_sc_set;

    is_sc_set = sclab_info.global_flags & S390_SCLAB_SC;
    if (is_sc_set &&
        sclab_info.signed_count != 1 &&
        sclab_info.unsigned_count >= 0) {
        comp_list->ipl_info_header.iiei |= S390_IIEI_MORE_SIGNED_COMP;
        zipl_secure_error("Only one signed component is allowed");
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
        zipl_secure_error("Global SCLAB does not exists");
        return;
    }

    if (sclab_info.global_count > 1) {
        comp_list->ipl_info_header.iiei |= S390_IIEI_MORE_GLOBAL_SCLAB;
        zipl_secure_error("More than one global SCLAB");
        return;
    }

    if (sclab_info.global_flags) {
        /* Unsigned components are not allowed if NUC flag is set in the global SCLAB */
        check_no_unsigned_comp(sclab_info, comp_list);

        /* Only one signed component is allowed is SC flag is set in the global SCLAB */
        check_single_comp(sclab_info, comp_list);
    }
}

static void check_sclab(SecureIplCompEntryInfo *comp_entry_info,
                        SecureIplSclabInfo *sclab_info)
{
    SclabOriginLocator *sclab_locator;
    SclaBlock *sclab;

    /* sclab locator is located at the last 8 bytes of the signed comp */
    sclab_locator = (SclabOriginLocator *)(comp_entry_info->addr +
                                           comp_entry_info->len - 8);

    /* return early if sclab does not exist */
    zipl_secure_validate(magic_match(sclab_locator->magic, ZIPL_MAGIC),
                         &comp_entry_info->cei, S390_CEI_INVALID_SCLAB,
                         "Magic does not match. SCLAB does not exist");

    if (comp_entry_info->cei & S390_CEI_INVALID_SCLAB) {
        return;
    }

    zipl_secure_validate(sclab_locator->len >= S390_SCLAB_MIN_LEN, &comp_entry_info->cei,
                         S390_CEI_INVALID_SCLAB_LEN | S390_CEI_INVALID_SCLAB,
                         "Invalid SCLAB length");

    /* return early if sclab is invalid */
    if (comp_entry_info->cei & S390_CEI_INVALID_SCLAB) {
        return;
    }

    sclab_info->count += 1;
    sclab = (SclaBlock *)(comp_entry_info->addr + comp_entry_info->len -
                          sclab_locator->len);

    zipl_secure_validate(sclab->format == 0, &comp_entry_info->cei,
                         S390_CEI_INVALID_SCLAB_FORMAT,
                         "Format-0 SCLAB is not being used");

    check_sclab_opsw(sclab, sclab_info, &comp_entry_info->cei);
    check_sclab_ola(sclab, comp_entry_info->addr, &comp_entry_info->cei);

    zipl_secure_validate(~sclab->flags & S390_SCLAB_NUC || sclab->flags & S390_SCLAB_OPSW,
                         &comp_entry_info->cei, S390_CEI_NUC_NOT_IN_GLOBAL_SCLAB,
                         "NUC bit is set, but not in the global SCLAB");

    zipl_secure_validate(~sclab->flags & S390_SCLAB_SC || sclab->flags & S390_SCLAB_OPSW,
                         &comp_entry_info->cei, S390_CEI_SC_NOT_IN_GLOBAL_SCLAB,
                         "SC bit is set, but not in the global SCLAB");

}

static int zipl_load_signature(ComponentEntry *entry, uint64_t sig)
{
    if (entry->compdat.sig_info.format != DER_SIGNATURE_FORMAT) {
        puts("Signature is not in DER format");
        return -1;
    }

    if (zipl_load_segment(entry->data.blockno, sig) < 0) {
        return -1;
    }

    return entry->compdat.sig_info.sig_len;
}

int zipl_run_secure(ComponentEntry **entry_ptr, uint8_t *tmp_sec)
{
    IplDeviceComponentList comp_list = { 0 };
    IplSignatureCertificateList cert_list = { 0 };
    SecureIplCompEntryInfo sig_entry_info = { 0 };
    SecureIplCompEntryInfo comp_entry_info;
    ComponentEntry *entry = *entry_ptr;
    uint8_t *cert_buf = NULL;
    int sig_len = 0;
    int comp_len;
    int cert_entry_idx;
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

    init_lists(&comp_list, &cert_list);
    cert_buf = malloc(get_total_certs_length());
    sig_entry_info.addr = (uint64_t)malloc(MAX_SECTOR_SIZE);

    while (entry->component_type != ZIPL_COMP_ENTRY_EXEC) {
        switch (entry->component_type) {
        case ZIPL_COMP_ENTRY_SIGNATURE:
            if (sig_entry_info.len) {
                goto out;
            }

            sig_len = zipl_load_signature(entry, sig_entry_info.addr);
            if (sig_len < 0) {
                goto out;
            }

            sig_entry_info.len = sig_len;
            break;
        case ZIPL_COMP_ENTRY_LOAD:
            comp_addr = entry->compdat.load_addr;
            comp_len = zipl_load_segment(entry->data.blockno, comp_addr);
            if (comp_len < 0) {
                goto out;
            }

            comp_entry_info = (SecureIplCompEntryInfo){ 0 };
            comp_entry_info.addr = comp_addr;
            comp_entry_info.len = (uint64_t)comp_len;

            check_comp_overlap(&range_list, comp_entry_info);
            comp_addr_range_add(&range_list, comp_entry_info, !!sig_len);

            /* no signature present (unsigned component) */
            if (!sig_entry_info.len) {
                zipl_secure_validate(comp_entry_info.addr >= S390_UNSIGNED_MIN_ADDR,
                                     &comp_entry_info.cei, S390_CEI_INVALID_UNSIGNED_ADDR,
                                     "Load address is less than 0x2000");

                comp_list_add(&comp_list, comp_entry_info);

                sclab_info.unsigned_count += 1;
                break;
            }

            /*
             * Initialize with SC flag (signed component)
             * CSV flag set upon successful verification
             */
            comp_entry_info.flags = S390_IPL_DEV_COMP_FLAG_SC;

            check_sclab(&comp_entry_info, &sclab_info);
            verified = verify_signature(comp_entry_info, sig_entry_info,
                                        &cert_len, &cert_table_idx);

            if (verified) {
                if (cert_list_table[cert_table_idx] == -1) {
                    if (!request_certificate(cert_buf, cert_table_idx)) {
                        puts("Could not get certificate");
                        goto out;
                    }

                    cert_entry_idx = cert_list_add(&cert_list, cert_buf, cert_len);
                    /* map cert-store index to cert-list entry index */
                    cert_list_table[cert_table_idx] = cert_entry_idx;
                    /* increment for the next certificate */
                    cert_buf += cert_len;
                }

                comp_entry_info.cert_index = cert_list_table[cert_table_idx];
                comp_entry_info.flags |= S390_IPL_DEV_COMP_FLAG_CSV;
                puts("Verified component");
            } else {
                zipl_secure_error("Could not verify component");
            }

            comp_list_add(&comp_list, comp_entry_info);

            sclab_info.signed_count += 1;
            /* After a signature is used another new one can be accepted */
            sig_entry_info.len = 0;
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
        comp_list_add(&comp_list, comp_entry_info);
    }

    zipl_secure_validate(sclab_info.signed_count > 0,
                         &comp_list.ipl_info_header.iiei, S390_IIEI_NO_SIGNED_COMP,
                         "Secure boot is on, but components are not signed");

    zipl_secure_validate(sclab_info.count > 0, &comp_list.ipl_info_header.iiei,
                         S390_IIEI_NO_SCLAB, "No recognizable SCLAB");

    check_global_sclab(sclab_info, &comp_list);

    update_iirb(&comp_list, &cert_list);

    *entry_ptr = entry;
    free((void *)sig_entry_info.addr);

    return 0;
out:
    free(cert_buf);
    free((void *)sig_entry_info.addr);

    return -1;
}
