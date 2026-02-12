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
    int signed_count = 0;

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

            if (!sig_len) {
                break;
            }

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

    if (signed_count == 0) {
        zipl_secure_handle("Secure boot is on, but components are not signed");
    }

    update_iirb(&comp_list, &cert_list);

    *entry_ptr = entry;
    free(sig);

    return 0;
out:
    free(cert_addr);
    free(sig);

    return -1;
}
