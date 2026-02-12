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
    uint32_t max_single_vcb_len;

    /* Get Verification Certificate Storage Size block with DIAG320 subcode 1 */
    vcssb = zipl_secure_get_vcssb();
    if (vcssb == NULL) {
        return 0;
    }

    /*
     * Request single entry
     * Fill input fields of single-entry VCB
     */
    max_single_vcb_len = ROUND_UP(vcssb->max_single_vcb_len, PAGE_SIZE);
    vcb = malloc(max_single_vcb_len);
    vcb->in_len = max_single_vcb_len;
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
    free(vcb);
    return cert_len;
}

static void cert_list_add(IplSignatureCertificateList *cert_list, int cert_index,
                          uint8_t *cert_addr, uint64_t cert_len)
{
    if (cert_index > MAX_CERTIFICATES - 1) {
        printf("Warning: Ignoring cert entry #%d because only %d entries are supported\n",
                cert_index + 1, MAX_CERTIFICATES);
        return;
    }

    cert_list->cert_entries[cert_index].addr = (uint64_t)cert_addr;
    cert_list->cert_entries[cert_index].len = cert_len;
    cert_list->ipl_info_header.len += sizeof(cert_list->cert_entries[cert_index]);
}

static void comp_list_add(IplDeviceComponentList *comp_list, int comp_index,
                          int cert_index, uint64_t comp_addr,
                          uint64_t comp_len, uint8_t flags)
{
    if (comp_index > MAX_CERTIFICATES - 1) {
        printf("Warning: Ignoring comp entry #%d because only %d entries are supported\n",
                comp_index + 1, MAX_CERTIFICATES);
        return;
    }

    comp_list->device_entries[comp_index].addr = comp_addr;
    comp_list->device_entries[comp_index].len = comp_len;
    comp_list->device_entries[comp_index].flags = flags;
    comp_list->device_entries[comp_index].cert_index = cert_index;
    comp_list->ipl_info_header.len += sizeof(comp_list->device_entries[comp_index]);
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

static bool is_comp_overlap(SecureIplCompAddrRange *comp_addr_range,
                            int addr_range_index,
                            uint64_t start_addr, uint64_t end_addr)
{
    /* neither a signed nor an unsigned component can overlap with a signed component */
    for (int i = 0; i < addr_range_index; i++) {
        if ((comp_addr_range[i].start_addr < end_addr &&
            start_addr < comp_addr_range[i].end_addr) &&
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
    if (addr_range_index >= MAX_CERTIFICATES) {
        printf("Warning: Ignoring component address range index [%d]"
               " because the maximum supported index is %d\n",
               addr_range_index, MAX_CERTIFICATES - 1);
        return;
    }

    comp_addr_range[addr_range_index].is_signed = is_signed;
    comp_addr_range[addr_range_index].start_addr = start_addr;
    comp_addr_range[addr_range_index].end_addr = end_addr;
}

static void addr_overlap_check(SecureIplCompAddrRange *comp_addr_range,
                               int *addr_range_index,
                               uint64_t start_addr, uint64_t end_addr, bool is_signed)
{
    bool overlap;

    overlap = is_comp_overlap(comp_addr_range, *addr_range_index,
                              start_addr, end_addr);
    if (overlap) {
        zipl_secure_handle("Component addresses overlap");
    }

    comp_addr_range_add(comp_addr_range, *addr_range_index, is_signed,
                        start_addr, end_addr);
    *addr_range_index += 1;
}

static void check_unsigned_addr(uint64_t load_addr, IplDeviceComponentEntry *comp_entry)
{
    /* unsigned load address must be greater than or equal to 0x2000 */
    if (load_addr >= 0x2000) {
        return;
    }

    set_comp_cei_with_log(comp_entry, S390_CEI_INVALID_UNSIGNED_ADDR,
                          "Load address is less than 0x2000");
}

static bool check_sclab_presence(uint8_t *sclab_magic,
                                 IplDeviceComponentEntry *comp_entry)
{
    /* identifies the presence of SCLAB */
    if (magic_match(sclab_magic, ZIPL_MAGIC)) {
        return true;
    }

    if (comp_entry) {
        comp_entry->cei |= S390_CEI_INVALID_SCLAB;
    }

    /* a missing SCLAB will not be reported in audit mode */
    if (boot_mode == ZIPL_BOOT_MODE_SECURE) {
        zipl_secure_handle("Magic does not match. SCLAB does not exist");
    }

    return false;
}

static void check_sclab_length(uint16_t sclab_len, IplDeviceComponentEntry *comp_entry)
{
    if (sclab_len >= S390_SECURE_IPL_SCLAB_MIN_LEN) {
        return;
    }

    set_comp_cei_with_log(comp_entry,
                          S390_CEI_INVALID_SCLAB_LEN | S390_CEI_INVALID_SCLAB,
                          "Invalid SCLAB length");
}

static void check_sclab_format(uint8_t sclab_format, IplDeviceComponentEntry *comp_entry)
{
    /* SCLAB format must set to zero, indicating a format-0 SCLAB being used */
    if (sclab_format == 0) {
        return;
    }

    set_comp_cei_with_log(comp_entry, S390_CEI_INVALID_SCLAB_FORMAT,
                          "Format-0 SCLAB is not being used");
}

static void check_sclab_opsw(SecureCodeLoadingAttributesBlock *sclab,
                             SecureIplSclabInfo *sclab_info,
                             IplDeviceComponentEntry *comp_entry)
{
    const char *msg;
    uint32_t cei_flag = 0;

    if (!(sclab->flags & S390_SECURE_IPL_SCLAB_FLAG_OPSW)) {
        /* OPSW = 0 - Load PSW field in SCLAB must contain zeros */
        if (sclab->load_psw != 0) {
            cei_flag |= S390_CEI_SCLAB_LOAD_PSW_NOT_ZERO;
            msg = "Load PSW is not zero when Override PSW bit is zero";
        }
    } else {
        /* OPSW = 1 indicating global SCLAB */
        sclab_info->global_count += 1;
        if (sclab_info->global_count == 1) {
            sclab_info->load_psw = sclab->load_psw;
            sclab_info->flags = sclab->flags;
        }

        /* OLA must set to one */
        if (!(sclab->flags & S390_SECURE_IPL_SCLAB_FLAG_OLA)) {
            cei_flag |= S390_CEI_SCLAB_OLA_NOT_ONE;
            msg = "Override Load Address bit is not set to one in the global SCLAB";
        }
    }

    if (!cei_flag) {
        return;
    }

    set_comp_cei_with_log(comp_entry, cei_flag, msg);
}

static void check_sclab_ola(SecureCodeLoadingAttributesBlock *sclab, uint64_t load_addr,
                            IplDeviceComponentEntry *comp_entry)
{
    const char *msg;
    uint32_t cei_flag = 0;

    if (!(sclab->flags & S390_SECURE_IPL_SCLAB_FLAG_OLA)) {
        /* OLA = 0 - Load address field in SCLAB must contain zeros */
        if (sclab->load_addr != 0) {
            cei_flag |= S390_CEI_SCLAB_LOAD_ADDR_NOT_ZERO;
            msg = "Load Address is not zero when Override Load Address bit is zero";
        }
    } else {
        /* OLA = 1 - Load address field must match storage address of the component */
        if (sclab->load_addr != load_addr) {
            cei_flag |= S390_CEI_UNMATCHED_SCLAB_LOAD_ADDR;
            msg = "Load Address does not match with component load address";
        }
    }

    if (!cei_flag) {
        return;
    }

    set_comp_cei_with_log(comp_entry, cei_flag, msg);
}

static void check_sclab_nuc(uint16_t sclab_flags, IplDeviceComponentEntry *comp_entry)
{
    const char *msg;
    bool is_nuc_set;
    bool is_global_sclab;

    is_nuc_set = sclab_flags & S390_SECURE_IPL_SCLAB_FLAG_NUC;
    is_global_sclab = sclab_flags & S390_SECURE_IPL_SCLAB_FLAG_OPSW;
    if (is_nuc_set && !is_global_sclab) {
        msg = "No Unsigned Components bit is set, but not in the global SCLAB";
        set_comp_cei_with_log(comp_entry, S390_CEI_NUC_NOT_IN_GLOBAL_SCLA, msg);
    }
}

static void check_sclab_sc(uint16_t sclab_flags, IplDeviceComponentEntry *comp_entry)
{
    const char *msg;
    bool is_sc_set;
    bool is_global_sclab;

    is_sc_set = sclab_flags & S390_SECURE_IPL_SCLAB_FLAG_SC;
    is_global_sclab = sclab_flags & S390_SECURE_IPL_SCLAB_FLAG_OPSW;
    if (is_sc_set && !is_global_sclab) {
        msg = "Single Component bit is set, but not in the global SCLAB";
        set_comp_cei_with_log(comp_entry, S390_CEI_SC_NOT_IN_GLOBAL_SCLAB, msg);
    }
}

static bool is_psw_valid(uint64_t psw, SecureIplCompAddrRange *comp_addr_range,
                         int range_index)
{
    uint32_t addr = psw & 0x7fffffff;

    /* PSW points within a signed binary code component */
    for (int i = 0; i < range_index; i++) {
        if (comp_addr_range[i].is_signed &&
            addr >= comp_addr_range[i].start_addr &&
            addr <= comp_addr_range[i].end_addr - 2) {
            return true;
       }
    }

    return false;
}

static void check_load_psw(SecureIplCompAddrRange *comp_addr_range,
                           int addr_range_index, uint64_t sclab_load_psw,
                           uint64_t load_psw, IplDeviceComponentEntry *comp_entry)
{
    bool valid;

    valid = is_psw_valid(sclab_load_psw, comp_addr_range, addr_range_index) &&
            is_psw_valid(load_psw, comp_addr_range, addr_range_index);
    if (!valid) {
        set_comp_cei_with_log(comp_entry, S390_CEI_INVALID_LOAD_PSW, "Invalid PSW");
    }

    /* compare load PSW with the PSW specified in component */
    if (sclab_load_psw != load_psw) {
        set_comp_cei_with_log(comp_entry, S390_CEI_UNMATCHED_SCLAB_LOAD_PSW,
                              "Load PSW does not match with PSW in component");
    }
}

static void check_nuc(uint16_t global_sclab_flags, int unsigned_count,
                      IplDeviceComponentList *comp_list)
{
    bool is_nuc_set;

    is_nuc_set = global_sclab_flags & S390_SECURE_IPL_SCLAB_FLAG_NUC;
    if (is_nuc_set && unsigned_count > 0) {
        comp_list->ipl_info_header.iiei |= S390_IIEI_FOUND_UNSIGNED_COMP;
        zipl_secure_handle("Unsigned components are not allowed");
    }
}

static void check_sc(uint16_t global_sclab_flags,
                     int signed_count, int unsigned_count,
                     IplDeviceComponentList *comp_list)
{
    bool is_sc_set;

    is_sc_set = global_sclab_flags & S390_SECURE_IPL_SCLAB_FLAG_SC;
    if (is_sc_set && signed_count != 1 && unsigned_count >= 0) {
        comp_list->ipl_info_header.iiei |= S390_IIEI_MORE_SIGNED_COMP;
        zipl_secure_handle("Only one signed component is allowed");
    }
}

void check_global_sclab(SecureIplSclabInfo sclab_info,
                        int unsigned_count, int signed_count,
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

    if (sclab_info.flags) {
        /* Unsigned components are not allowed if NUC flag is set in the global SCLAB */
        check_nuc(sclab_info.flags, unsigned_count, comp_list);

        /* Only one signed component is allowed is SC flag is set in the global SCLAB */
        check_sc(sclab_info.flags, signed_count, unsigned_count, comp_list);
    }
}

static void check_signed_comp(int signed_count, IplDeviceComponentList *comp_list)
{
    if (signed_count > 0) {
        return;
    }

    comp_list->ipl_info_header.iiei |= S390_IIEI_NO_SIGNED_COMP;
    zipl_secure_handle("Secure boot is on, but components are not signed");
}

static void check_sclab_count(int count, IplDeviceComponentList *comp_list)
{
    if (count > 0) {
        return;
    }

    comp_list->ipl_info_header.iiei |= S390_IIEI_NO_SCLAB;
    zipl_secure_handle("No recognizable SCLAB");
}

static void check_sclab(uint64_t comp_addr, uint64_t comp_len,
                        IplDeviceComponentEntry *comp_entry,
                        SecureIplSclabInfo *sclab_info)
{
    SclabOriginLocator *sclab_locator;
    SecureCodeLoadingAttributesBlock *sclab;
    bool exist;

    /* sclab locator is located at the last 8 bytes of the signed comp */
    sclab_locator = (SclabOriginLocator *)(comp_addr + comp_len - 8);

    /* return early if sclab does not exist */
    exist = check_sclab_presence(sclab_locator->magic, comp_entry);
    if (!exist) {
        return;
    }

    check_sclab_length(sclab_locator->len, comp_entry);

    /* return early if sclab is invalid */
    if (comp_entry && (comp_entry->cei & S390_CEI_INVALID_SCLAB)) {
        return;
    }

    sclab_info->count += 1;
    sclab = (SecureCodeLoadingAttributesBlock *)(comp_addr + comp_len -
                                                 sclab_locator->len);

    check_sclab_format(sclab->format, comp_entry);
    check_sclab_opsw(sclab, sclab_info, comp_entry);
    check_sclab_ola(sclab, comp_addr, comp_entry);
    check_sclab_nuc(sclab->flags, comp_entry);
    check_sclab_sc(sclab->flags, comp_entry);
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
    ComponentEntry *entry = *entry_ptr;
    uint8_t *cert_addr = NULL;
    uint64_t *sig = NULL;
    int cert_entry_idx = 0;
    int comp_entry_idx = 0;
    uint64_t comp_addr;
    int comp_len;
    uint32_t sig_len = 0;
    uint64_t cert_len = -1;
    uint8_t cert_table_idx = -1;
    int cert_index;
    uint8_t flags;
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
    SecureIplCompAddrRange comp_addr_range[MAX_CERTIFICATES];
    int addr_range_index = 0;
    int signed_count = 0;
    int unsigned_count = 0;
    SecureIplSclabInfo sclab_info = { 0 };
    IplDeviceComponentEntry *comp_entry;

    if (!secure_ipl_supported()) {
        return -1;
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

            addr_overlap_check(comp_addr_range, &addr_range_index,
                               comp_addr, comp_addr + comp_len, sig_len > 0);

            comp_entry = (comp_entry_idx < MAX_CERTIFICATES) ?
                         &comp_list.device_entries[comp_entry_idx] : NULL;

            if (!sig_len) {
                check_unsigned_addr(comp_addr, comp_entry);
                comp_list_add(&comp_list, comp_entry_idx, cert_entry_idx,
                              comp_addr, comp_len, 0x00);

                unsigned_count += 1;
                comp_entry_idx++;
                break;
            }

            check_sclab(comp_addr, comp_len,
                        &comp_list.device_entries[comp_entry_idx], &sclab_info);
            verified = verify_signature(comp_len, comp_addr, sig_len, (uint64_t)sig,
                                        &cert_len, &cert_table_idx);

            /* default cert index and flags for unverified component */
            cert_index = -1;
            flags = S390_IPL_DEV_COMP_FLAG_SC;

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
                cert_index = cert_list_table[cert_table_idx];
                flags |= S390_IPL_DEV_COMP_FLAG_CSV;
            }

            comp_list_add(&comp_list, comp_entry_idx, cert_index,
                          comp_addr, comp_len, flags);

            if (!verified) {
                zipl_secure_handle("Could not verify component");
            }

            comp_entry_idx++;
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

    /* validate load PSW with PSW specified in the final entry */
    if (sclab_info.load_psw) {
        comp_entry = (comp_entry_idx < MAX_CERTIFICATES) ?
                     &comp_list.device_entries[comp_entry_idx] : NULL;
        check_load_psw(comp_addr_range, addr_range_index,
                       sclab_info.load_psw, entry->compdat.load_psw, comp_entry);
        comp_list_add(&comp_list, comp_entry_idx, -1,
                      entry->compdat.load_psw, 0, 0x00);
    }

    check_signed_comp(signed_count, &comp_list);
    check_sclab_count(sclab_info.count, &comp_list);
    check_global_sclab(sclab_info, unsigned_count, signed_count, &comp_list);

    update_iirb(&comp_list, &cert_list);

    *entry_ptr = entry;
    free(sig);

    return 0;
out:
    free(cert_addr);
    free(sig);

    return -1;
}
