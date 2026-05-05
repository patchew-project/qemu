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
    int signed_count = 0;

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

            /* no signature present (unsigned component) */
            if (!sig_entry_info.len) {
                break;
            }

            /*
             * Initialize with SC flag (signed component)
             * CSV flag set upon successful verification
             */
            comp_entry_info.flags = S390_IPL_DEV_COMP_FLAG_SC;

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

            signed_count += 1;
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

    if (signed_count == 0) {
        zipl_secure_error("Secure boot is on, but components are not signed");
    }

    update_iirb(&comp_list, &cert_list);

    *entry_ptr = entry;
    free((void *)sig_entry_info.addr);

    return 0;
out:
    free(cert_buf);
    free((void *)sig_entry_info.addr);

    return -1;
}
