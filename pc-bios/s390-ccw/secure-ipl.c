/*
 * S/390 Secure IPL
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>
#include <stdio.h>
#include "s390-ccw.h"
#include "secure-ipl.h"

uint8_t vcb_data[MAX_SECTOR_SIZE * 4] __attribute__((__aligned__(PAGE_SIZE)));
uint8_t vcssb_data[VCSSB_MAX_LEN] __attribute__((__aligned__(PAGE_SIZE)));

VCStorageSizeBlock *zipl_secure_get_vcssb(void)
{
    VCStorageSizeBlock *vcssb;
    int rc;

    vcssb = (VCStorageSizeBlock *)vcssb_data;
    /* avoid retrieving vcssb multiple times */
    if (vcssb->length == VCSSB_MAX_LEN) {
        return vcssb;
    }

    rc = diag320(vcssb, DIAG_320_SUBC_QUERY_VCSI);
    if (rc != DIAG_320_RC_OK) {
        return NULL;
    }

    return vcssb;
}

uint32_t zipl_secure_get_certs_length(void)
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

uint32_t zipl_secure_request_certificate(uint64_t *cert, uint8_t index)
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
    vcb = (VCBlock *)vcb_data;
    vcb->in_len = ROUND_UP(vcssb->max_single_vcb_len, PAGE_SIZE);
    vcb->first_vc_index = index + 1;
    vcb->last_vc_index = index + 1;

    rc = diag320(vcb, DIAG_320_SUBC_STORE_VC);
    if (rc == DIAG_320_RC_OK) {
        vce = (VCEntry *)vcb->vce_buf;
        cert_len = vce->cert_len;
        memcpy(cert, (uint8_t *)vce + vce->cert_offset, vce->cert_len);
        /* clear out region for next cert(s) */
        memcpy(vcb_data, 0, sizeof(vcb_data));
    }

    return cert_len;
}

void zipl_secure_cert_list_add(IplSignatureCertificateList *certs, int cert_index,
                               uint64_t *cert, uint64_t cert_len)
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

void zipl_secure_comp_list_add(IplDeviceComponentList *comps, int comp_index,
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

int zipl_secure_update_iirb(IplDeviceComponentList *comps,
                            IplSignatureCertificateList *certs)
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

bool zipl_secure_ipl_supported(void)
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

void zipl_secure_init_lists(IplDeviceComponentList *comps,
                            IplSignatureCertificateList *certs)
{
    comps->ipl_info_header.ibt = IPL_IBT_COMPONENTS;
    comps->ipl_info_header.len = sizeof(comps->ipl_info_header);

    certs->ipl_info_header.ibt = IPL_IBT_CERTIFICATES;
    certs->ipl_info_header.len = sizeof(certs->ipl_info_header);
}
