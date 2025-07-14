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
#include "hw/s390x/ipl.h"
#include "hw/s390x/ipl/diag320.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "system/kvm.h"
#include "kvm/kvm_s390x.h"
#include "target/s390x/kvm/pv.h"
#include "qemu/error-report.h"


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

QEMU_BUILD_BUG_MSG(sizeof(VCStorageSizeBlock) != 128,
                   "size of VCStorageSizeBlock is wrong");

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

    if (!s390_has_feat(S390_FEAT_DIAG_320)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    if (r1 & 1) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    switch (subcode) {
    case DIAG_320_SUBC_QUERY_ISM:
        uint64_t ism = cpu_to_be64(DIAG_320_ISM_QUERY_VCSI);

        if (s390_cpu_virt_mem_write(cpu, addr, r1, &ism, sizeof(ism))) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }

        rc = DIAG_320_RC_OK;
        break;
    case DIAG_320_SUBC_QUERY_VCSI:
        VCStorageSizeBlock vcssb;

        if (!diag_parm_addr_valid(addr, sizeof(VCStorageSizeBlock),
                                  true)) {
            s390_program_interrupt(env, PGM_ADDRESSING, ra);
            return;
        }

        if (!qcs || !qcs->count) {
            vcssb.length = cpu_to_be32(4);
        } else {
            vcssb.length = cpu_to_be32(VCSSB_MAX_LEN);
            vcssb.version = 0;
            vcssb.total_vc_ct = cpu_to_be16(qcs->count);
            vcssb.max_vc_ct = cpu_to_be16(MAX_CERTIFICATES);
            vcssb.max_vce_len = cpu_to_be32(VCE_HEADER_LEN + qcs->max_cert_size);
            vcssb.max_single_vcb_len = cpu_to_be32(VCB_HEADER_LEN + VCE_HEADER_LEN +
                                                   qcs->max_cert_size);
            vcssb.total_vcb_len = cpu_to_be32(VCB_HEADER_LEN +
                                              qcs->count * VCE_HEADER_LEN +
                                              qcs->total_bytes);
        }

        if (s390_cpu_virt_mem_write(cpu, addr, r1, &vcssb, sizeof(VCStorageSizeBlock))) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }
        rc = DIAG_320_RC_OK;
        break;
    default:
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }
    env->regs[r1 + 1] = rc;
}
