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

    return true;
}

static void init_lists(IplDeviceComponentList *comps, IplSignatureCertificateList *certs)
{
    comps->ipl_info_header.ibt = IPL_IBT_COMPONENTS;
    comps->ipl_info_header.len = sizeof(comps->ipl_info_header);

    certs->ipl_info_header.ibt = IPL_IBT_CERTIFICATES;
    certs->ipl_info_header.len = sizeof(certs->ipl_info_header);
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
    int signed_count = 0;

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

            if (!sig_len) {
                break;
            }

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

    if (signed_count == 0) {
        zipl_secure_handle("Secure boot is on, but components are not signed");
    }

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
