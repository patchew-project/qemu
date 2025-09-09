/*
 * S390x DIAG instruction helper functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "hw/watchdog/wdt_diag288.h"
#include "system/cpus.h"
#include "hw/s390x/cert-store.h"
#include "hw/s390x/ipl.h"
#include "hw/s390x/ipl/diag320.h"
#include "hw/s390x/ipl/diag508.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "system/kvm.h"
#include "kvm/kvm_s390x.h"
#include "target/s390x/kvm/pv.h"
#include "qemu/error-report.h"
#include "crypto/x509-utils.h"


int handle_diag_288(CPUS390XState *env, uint64_t r1, uint64_t r3)
{
    uint64_t func = env->regs[r1];
    uint64_t timeout = env->regs[r1 + 1];
    uint64_t action = env->regs[r3];
    Object *obj;
    DIAG288State *diag288;
    DIAG288Class *diag288_class;

    if (r1 % 2 || action != 0) {
        return -1;
    }

    /* Timeout must be more than 15 seconds except for timer deletion */
    if (func != WDT_DIAG288_CANCEL && timeout < 15) {
        return -1;
    }

    obj = object_resolve_path_type("", TYPE_WDT_DIAG288, NULL);
    if (!obj) {
        return -1;
    }

    diag288 = DIAG288(obj);
    diag288_class = DIAG288_GET_CLASS(diag288);
    return diag288_class->handle_timer(diag288, func, timeout);
}

static int diag308_parm_check(CPUS390XState *env, uint64_t r1, uint64_t addr,
                              uintptr_t ra, bool write)
{
    /* Handled by the Ultravisor */
    if (s390_is_pv()) {
        return 0;
    }
    if ((r1 & 1) || (addr & ~TARGET_PAGE_MASK)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return -1;
    }
    if (!diag_parm_addr_valid(addr, sizeof(IplParameterBlock), write)) {
        s390_program_interrupt(env, PGM_ADDRESSING, ra);
        return -1;
    }
    return 0;
}

void handle_diag_308(CPUS390XState *env, uint64_t r1, uint64_t r3, uintptr_t ra)
{
    bool valid;
    CPUState *cs = env_cpu(env);
    S390CPU *cpu = env_archcpu(env);
    uint64_t addr =  env->regs[r1];
    uint64_t subcode = env->regs[r3];
    IplParameterBlock *iplb;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return;
    }

    if (subcode & ~0x0ffffULL) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    if (subcode >= DIAG308_PV_SET && !s390_has_feat(S390_FEAT_UNPACK)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    switch (subcode) {
    case DIAG308_RESET_MOD_CLR:
        s390_ipl_reset_request(cs, S390_RESET_MODIFIED_CLEAR);
        break;
    case DIAG308_RESET_LOAD_NORM:
        s390_ipl_reset_request(cs, S390_RESET_LOAD_NORMAL);
        break;
    case DIAG308_LOAD_CLEAR:
        /* Well we still lack the clearing bit... */
        s390_ipl_reset_request(cs, S390_RESET_REIPL);
        break;
    case DIAG308_SET:
    case DIAG308_PV_SET:
        if (diag308_parm_check(env, r1, addr, ra, false)) {
            return;
        }
        iplb = g_new0(IplParameterBlock, 1);
        if (!s390_is_pv()) {
            cpu_physical_memory_read(addr, iplb, sizeof(iplb->len));
        } else {
            s390_cpu_pv_mem_read(cpu, 0, iplb, sizeof(iplb->len));
        }

        if (!iplb_valid_len(iplb)) {
            env->regs[r1 + 1] = DIAG_308_RC_INVALID;
            goto out;
        }

        if (!s390_is_pv()) {
            cpu_physical_memory_read(addr, iplb, be32_to_cpu(iplb->len));
        } else {
            s390_cpu_pv_mem_read(cpu, 0, iplb, be32_to_cpu(iplb->len));
        }

        valid = subcode == DIAG308_PV_SET ? iplb_valid_pv(iplb) : iplb_valid(iplb);
        if (!valid) {
            if (subcode == DIAG308_SET && iplb->pbt == S390_IPL_TYPE_QEMU_SCSI) {
                s390_rebuild_iplb(iplb->devno, iplb);
                s390_ipl_update_diag308(iplb);
                env->regs[r1 + 1] = DIAG_308_RC_OK;
            } else {
                env->regs[r1 + 1] = DIAG_308_RC_INVALID;
            }

            goto out;
        }

        s390_ipl_update_diag308(iplb);
        env->regs[r1 + 1] = DIAG_308_RC_OK;
out:
        g_free(iplb);
        return;
    case DIAG308_STORE:
    case DIAG308_PV_STORE:
        if (diag308_parm_check(env, r1, addr, ra, true)) {
            return;
        }
        if (subcode == DIAG308_PV_STORE) {
            iplb = s390_ipl_get_iplb_pv();
        } else {
            iplb = s390_ipl_get_iplb();
        }
        if (!iplb) {
            env->regs[r1 + 1] = DIAG_308_RC_NO_CONF;
            return;
        }

        if (!s390_is_pv()) {
            cpu_physical_memory_write(addr, iplb, be32_to_cpu(iplb->len));
        } else {
            s390_cpu_pv_mem_write(cpu, 0, iplb, be32_to_cpu(iplb->len));
        }
        env->regs[r1 + 1] = DIAG_308_RC_OK;
        return;
    case DIAG308_PV_START:
        iplb = s390_ipl_get_iplb_pv();
        if (!iplb) {
            env->regs[r1 + 1] = DIAG_308_RC_NO_PV_CONF;
            return;
        }

        if (kvm_enabled() && kvm_s390_get_hpage_1m()) {
            error_report("Protected VMs can currently not be backed with "
                         "huge pages");
            env->regs[r1 + 1] = DIAG_308_RC_INVAL_FOR_PV;
            return;
        }

        s390_ipl_reset_request(cs, S390_RESET_PV);
        break;
    default:
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        break;
    }
}

static int handle_diag320_query_vcsi(S390CPU *cpu, uint64_t addr, uint64_t r1,
                                     uintptr_t ra, S390IPLCertificateStore *qcs)
{
    g_autofree VCStorageSizeBlock *vcssb = NULL;

    vcssb = g_new0(VCStorageSizeBlock, 1);
    if (s390_cpu_virt_mem_read(cpu, addr, r1, vcssb, sizeof(*vcssb))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return -1;
    }

    if (be32_to_cpu(vcssb->length) < VCSSB_MIN_LEN) {
        return DIAG_320_RC_INVAL_VCSSB_LEN;
    }

    if (!qcs->count) {
        vcssb->length = cpu_to_be32(VCSSB_NO_VC);
    } else {
        vcssb->version = 0;
        vcssb->total_vc_ct = cpu_to_be16(qcs->count);
        vcssb->max_vc_ct = cpu_to_be16(MAX_CERTIFICATES);
        vcssb->max_single_vcb_len = cpu_to_be32(VCB_HEADER_LEN + VCE_HEADER_LEN +
                                                qcs->max_cert_size);
        vcssb->total_vcb_len = cpu_to_be32(VCB_HEADER_LEN + qcs->count * VCE_HEADER_LEN +
                                           qcs->total_bytes);
    }

    if (s390_cpu_virt_mem_write(cpu, addr, r1, vcssb, be32_to_cpu(vcssb->length))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return -1;
    }
    return DIAG_320_RC_OK;
}

static bool is_cert_valid(S390IPLCertificate qcert)
{
    int rc;
    Error *err = NULL;

    rc = qcrypto_x509_check_cert_times(qcert.raw, qcert.size, &err);
    if (rc != 0) {
        error_report_err(err);
        return false;
    }

    return true;
}

static void handle_key_id(VCEntry *vce, S390IPLCertificate qcert)
{
    int rc;
    g_autofree unsigned char *key_id_data = NULL;
    Error *err = NULL;

    /* key id and key id len */
    rc = qcrypto_x509_get_cert_key_id(qcert.raw, qcert.size,
                                      QCRYPTO_HASH_ALGO_SHA256,
                                      &key_id_data, &qcert.key_id_size, &err);
    if (rc < 0) {
        error_report_err(err);
        return;
    }
    vce->keyid_len = cpu_to_be16(qcert.key_id_size);

    memcpy(vce->cert_buf, key_id_data, qcert.key_id_size);
}

static int handle_hash(VCEntry *vce, S390IPLCertificate qcert, uint16_t keyid_field_len)
{
    int rc;
    uint16_t hash_offset;
    g_autofree void *hash_data = NULL;
    Error *err = NULL;

    /* hash and hash len */
    hash_data = g_malloc0(qcert.hash_size);
    rc = qcrypto_get_x509_cert_fingerprint(qcert.raw, qcert.size,
                                           QCRYPTO_HASH_ALGO_SHA256,
                                           hash_data, &qcert.hash_size, &err);
    if (rc < 0) {
        error_report_err(err);
        return -1;
    }
    vce->hash_len = cpu_to_be16(qcert.hash_size);

    /* hash type */
    vce->hash_type = DIAG_320_VCE_HASHTYPE_SHA2_256;

    hash_offset = VCE_HEADER_LEN + keyid_field_len;
    vce->hash_offset = cpu_to_be16(hash_offset);

    memcpy((uint8_t *)vce + hash_offset, hash_data, qcert.hash_size);

    return 0;
}

static int handle_cert(VCEntry *vce, S390IPLCertificate qcert, uint16_t hash_field_len)
{
    int rc;
    uint16_t cert_offset;
    g_autofree uint8_t *cert_der = NULL;
    Error *err = NULL;

    /* certificate in DER format */
    rc = qcrypto_x509_convert_cert_der(qcert.raw, qcert.size,
                                       &cert_der, &qcert.der_size, &err);
    if (rc < 0) {
        error_report_err(err);
        return -1;
    }
    vce->format = DIAG_320_VCE_FORMAT_X509_DER;
    vce->cert_len = cpu_to_be32(qcert.der_size);
    cert_offset = be16_to_cpu(vce->hash_offset) + hash_field_len;
    vce->cert_offset = cpu_to_be16(cert_offset);

    memcpy((uint8_t *)vce + cert_offset, cert_der, qcert.der_size);

    return 0;
}

static int build_vce_header(VCEntry *vce, S390IPLCertificate qcert, int idx)
{
    int algo;
    Error *err = NULL;

    vce->len = cpu_to_be32(VCE_HEADER_LEN);
    vce->cert_idx = cpu_to_be16(idx + 1);
    strncpy((char *)vce->name, (char *)qcert.vc_name, VC_NAME_LEN_BYTES);

    /* public key algorithm */
    algo = qcrypto_x509_get_pk_algorithm(qcert.raw, qcert.size, &err);
    if (algo < 0) {
        error_report_err(err);
        return -1;
    }

    if (algo == QCRYPTO_PK_ALGO_ECDSA) {
        vce->key_type = DIAG_320_VCE_KEYTYPE_ECDSA;
    } else {
        vce->key_type = DIAG_320_VCE_KEYTYPE_SELF_DESCRIBING;
    }

    return 0;
}

static int build_vce_data(VCEntry *vce, S390IPLCertificate qcert)
{
    uint16_t keyid_field_len;
    uint16_t hash_field_len;
    uint32_t cert_field_len;
    int rc;

    handle_key_id(vce, qcert);
    /* vce key id field length - can be 0 if failed to retrieve */
    keyid_field_len = ROUND_UP(be16_to_cpu(vce->keyid_len), 4);

    rc = handle_hash(vce, qcert, keyid_field_len);
    if (rc) {
        return -1;
    }
    hash_field_len = ROUND_UP(be16_to_cpu(vce->hash_len), 4);

    rc = handle_cert(vce, qcert, hash_field_len);
    if (rc || !is_cert_valid(qcert)) {
        return -1;
    }
    /* vce certificate field length */
    cert_field_len = ROUND_UP(be32_to_cpu(vce->cert_len), 4);

    /* The certificate is valid and VCE contains the certificate */
    vce->flags |= DIAG_320_VCE_FLAGS_VALID;

    /* Update vce length to reflect the acutal size used by vce */
    vce->len += cpu_to_be32(keyid_field_len + hash_field_len + cert_field_len);

    return 0;
}

static VCEntry *diag_320_build_vce(S390IPLCertificate qcert, uint32_t vce_len, int idx)
{
    g_autofree VCEntry *vce = NULL;
    int rc;

    /*
     * Construct VCE
     * Allocate enough memory for all certificate data (key id, hash and certificate).
     * Unused area following the VCE field contains zeros.
     */
    vce = g_malloc0(vce_len);
    rc = build_vce_header(vce, qcert, idx);
    if (rc) {
        vce->len = cpu_to_be32(VCE_INVALID_LEN);
        goto out;
    }
    vce->len = cpu_to_be32(VCE_HEADER_LEN);

    rc = build_vce_data(vce, qcert);
    if (rc) {
        vce->len = cpu_to_be32(VCE_INVALID_LEN);
    }

out:
    return g_steal_pointer(&vce);
}

static int handle_diag320_store_vc(S390CPU *cpu, uint64_t addr, uint64_t r1, uintptr_t ra,
                                   S390IPLCertificateStore *qcs)
{
    g_autofree VCBlock *vcb = NULL;
    size_t vce_offset;
    size_t remaining_space;
    uint32_t vce_len;
    uint16_t first_vc_index;
    uint16_t last_vc_index;
    uint32_t in_len;

    vcb = g_new0(VCBlock, 1);
    if (s390_cpu_virt_mem_read(cpu, addr, r1, vcb, sizeof(*vcb))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return -1;
    }

    in_len = be32_to_cpu(vcb->in_len);
    first_vc_index = be16_to_cpu(vcb->first_vc_index);
    last_vc_index = be16_to_cpu(vcb->last_vc_index);

    if (in_len % TARGET_PAGE_SIZE != 0) {
        return DIAG_320_RC_INVAL_VCB_LEN;
    }

    if (first_vc_index > last_vc_index) {
        return DIAG_320_RC_BAD_RANGE;
    }

    if (first_vc_index == 0) {
        /*
         * Zero is a valid index for the first and last VC index.
         * Zero index results in the VCB header and zero certificates returned.
         */
        if (last_vc_index == 0) {
            goto out;
        }

        /* DIAG320 certificate store remains a one origin for cert entries */
        vcb->first_vc_index = 1;
        first_vc_index = 1;
    }

    vce_offset = VCB_HEADER_LEN;
    vcb->out_len = VCB_HEADER_LEN;
    remaining_space = in_len - VCB_HEADER_LEN;

    for (int i = first_vc_index - 1; i < last_vc_index && i < qcs->count; i++) {
        VCEntry *vce;
        S390IPLCertificate qcert = qcs->certs[i];
        /*
         * Each VCE is word aligned.
         * Each variable length field within the VCE is also word aligned.
         */
        vce_len = VCE_HEADER_LEN +
                  ROUND_UP(qcert.key_id_size, 4) +
                  ROUND_UP(qcert.hash_size, 4) +
                  ROUND_UP(qcert.der_size, 4);

        /*
         * If there is no more space to store the cert,
         * set the remaining verification cert count and
         * break early.
         */
        if (remaining_space < vce_len) {
            vcb->remain_ct = cpu_to_be16(last_vc_index - i);
            break;
        }

        vce = diag_320_build_vce(qcert, vce_len, i);

        /* Write VCE */
        if (s390_cpu_virt_mem_write(cpu, addr + vce_offset, r1,
                                    vce, be32_to_cpu(vce->len))) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return -1;
        }

        vce_offset += be32_to_cpu(vce->len);
        vcb->out_len += be32_to_cpu(vce->len);
        remaining_space -= be32_to_cpu(vce->len);
        vcb->stored_ct++;

        g_free(vce);
    }

    vcb->out_len = cpu_to_be32(vcb->out_len);
    vcb->stored_ct = cpu_to_be16(vcb->stored_ct);

out:
    /*
     * Write VCB header
     * All VCEs have been populated with the latest information
     * and write VCB header last.
     */
    if (s390_cpu_virt_mem_write(cpu, addr, r1, vcb, VCB_HEADER_LEN)) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return -1;
    }

    return DIAG_320_RC_OK;
}

void handle_diag_320(CPUS390XState *env, uint64_t r1, uint64_t r3, uintptr_t ra)
{
    S390CPU *cpu = env_archcpu(env);
    S390IPLCertificateStore *qcs = s390_ipl_get_certificate_store();
    uint64_t subcode = env->regs[r3];
    uint64_t addr = env->regs[r1];
    int rc;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return;
    }

    if (!s390_has_feat(S390_FEAT_CERT_STORE)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    if ((subcode & ~0x000ffULL) || (r1 & 1)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    switch (subcode) {
    case DIAG_320_SUBC_QUERY_ISM:
        /*
         * The Installed Subcode Block (ISB) can be up 8 words in size,
         * but the current set of subcodes can fit within a single word
         * for now.
         */
        uint32_t ism_word0 = cpu_to_be32(DIAG_320_ISM_QUERY_SUBCODES |
                                         DIAG_320_ISM_QUERY_VCSI |
                                         DIAG_320_ISM_STORE_VC);

        if (s390_cpu_virt_mem_write(cpu, addr, r1, &ism_word0, sizeof(ism_word0))) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }

        env->regs[r1 + 1] = DIAG_320_RC_OK;
        break;
    case DIAG_320_SUBC_QUERY_VCSI:
        if (!diag_parm_addr_valid(addr, sizeof(VCStorageSizeBlock), true)) {
            s390_program_interrupt(env, PGM_ADDRESSING, ra);
            return;
        }

        if (addr & 0x7) {
            s390_program_interrupt(env, PGM_ADDRESSING, ra);
            return;
        }

        rc = handle_diag320_query_vcsi(cpu, addr, r1, ra, qcs);
        if (rc == -1) {
            return;
        }
        env->regs[r1 + 1] = rc;
        break;
    case DIAG_320_SUBC_STORE_VC:
        rc = handle_diag320_store_vc(cpu, addr, r1, ra, qcs);
        if (rc == -1) {
            return;
        }
        env->regs[r1 + 1] = rc;
        break;
    default:
        env->regs[r1 + 1] = DIAG_320_RC_NOT_SUPPORTED;
        break;
    }
}

static int diag_508_verify_sig(uint8_t *cert, size_t cert_size,
                              uint8_t *comp, size_t comp_size,
                              uint8_t *sig, size_t sig_size)
{
    g_autofree uint8_t *sig_pem = NULL;
    size_t sig_size_pem;
    int rc;

    /*
     * PKCS#7 signature with DER format
     * Convert to PEM format for signature verification
     */
    rc = qcrypto_pkcs7_convert_sig_pem(sig, sig_size, &sig_pem, &sig_size_pem, NULL);
    if (rc < 0) {
        return -1;
    }

    /*
     * Ignore errors from signature format convertion and verification,
     * because currently in the certificate lookup process.
     *
     * Any error is treated as a verification failure,
     * and the final result (verified or not) will be reported later.
     */
    rc = qcrypto_x509_verify_sig(cert, cert_size,
                                 comp, comp_size,
                                 sig_pem, sig_size_pem, NULL);
    if (rc < 0) {
        return -1;
    }

    return 0;
}

static int handle_diag508_sig_verif(uint64_t addr, size_t csi_size, size_t svb_size,
                                    S390IPLCertificateStore *qcs)
{
    int rc;
    int verified;
    uint64_t comp_len, comp_addr;
    uint64_t sig_len, sig_addr;
    g_autofree uint8_t *svb_comp = NULL;
    g_autofree uint8_t *svb_sig = NULL;
    g_autofree Diag508SignatureVerificationBlock *svb = NULL;

    if (!qcs || !qcs->count) {
        return DIAG_508_RC_NO_CERTS;
    }

    svb = g_new0(Diag508SignatureVerificationBlock, 1);
    cpu_physical_memory_read(addr, svb, svb_size);

    comp_len = be64_to_cpu(svb->comp_len);
    comp_addr = be64_to_cpu(svb->comp_addr);
    sig_len = be64_to_cpu(svb->sig_len);
    sig_addr = be64_to_cpu(svb->sig_addr);

    if (!comp_len || !comp_addr) {
        return DIAG_508_RC_INVAL_COMP_DATA;
    }

    if (!sig_len || !sig_addr) {
        return DIAG_508_RC_INVAL_PKCS7_SIG;
    }

    svb_comp = g_malloc0(comp_len);
    cpu_physical_memory_read(comp_addr, svb_comp, comp_len);

    svb_sig = g_malloc0(sig_len);
    cpu_physical_memory_read(sig_addr, svb_sig, sig_len);

    rc = DIAG_508_RC_FAIL_VERIF;
    /*
     * It is uncertain which certificate contains
     * the analogous key to verify the signed data
     */
    for (int i = 0; i < qcs->count; i++) {
        verified = diag_508_verify_sig(qcs->certs[i].raw,
                                       qcs->certs[i].size,
                                       svb_comp, comp_len,
                                       svb_sig, sig_len);
        if (verified == 0) {
            svb->csi.idx = i;
            svb->csi.len = cpu_to_be64(qcs->certs[i].der_size);
            cpu_physical_memory_write(addr, &svb->csi, be32_to_cpu(csi_size));
            rc = DIAG_508_RC_OK;
            break;
       }
    }

    return rc;
}

QEMU_BUILD_BUG_MSG(sizeof(Diag508SignatureVerificationBlock) != 48,
                   "size of Diag508SignatureVerificationBlock is wrong");

void handle_diag_508(CPUS390XState *env, uint64_t r1, uint64_t r3, uintptr_t ra)
{
    S390IPLCertificateStore *qcs = s390_ipl_get_certificate_store();
    uint64_t subcode = env->regs[r3];
    uint64_t addr = env->regs[r1];
    int rc;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return;
    }

    if ((subcode & ~0x0ffffULL) || (r1 & 1)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    switch (subcode) {
    case DIAG_508_SUBC_QUERY_SUBC:
        rc = DIAG_508_SUBC_SIG_VERIF;
        break;
    case DIAG_508_SUBC_SIG_VERIF:
        size_t csi_size = sizeof(Diag508CertificateStoreInfo);
        size_t svb_size = sizeof(Diag508SignatureVerificationBlock);

        if (!diag_parm_addr_valid(addr, svb_size, false) ||
            !diag_parm_addr_valid(addr, csi_size, true)) {
            s390_program_interrupt(env, PGM_ADDRESSING, ra);
            return;
        }

        rc = handle_diag508_sig_verif(addr, csi_size, svb_size, qcs);
        break;
    default:
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }
    env->regs[r1 + 1] = rc;
}
