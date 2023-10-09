/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch KVM
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/kvm.h>

#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "sysemu/kvm_int.h"
#include "hw/pci/pci.h"
#include "exec/memattrs.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "hw/loader.h"
#include "migration/migration.h"
#include "sysemu/runstate.h"
#include "cpu-csr.h"
#include "kvm_loongarch.h"
#include "trace.h"

static bool cap_has_mp_state;
const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

static int kvm_loongarch_get_regs_core(CPUState *cs)
{
    int ret = 0;
    int i;
    struct kvm_regs regs;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    /* Get the current register set as KVM seems it */
    ret = kvm_vcpu_ioctl(cs, KVM_GET_REGS, &regs);
    if (ret < 0) {
        trace_kvm_failed_get_regs_core(strerror(errno));
        return ret;
    }
    /* gpr[0] value is always 0 */
    env->gpr[0] = 0;
    for (i = 1; i < 32; i++) {
        env->gpr[i] = regs.gpr[i];
    }

    env->pc = regs.pc;
    return ret;
}

static int kvm_loongarch_put_regs_core(CPUState *cs)
{
    int ret = 0;
    int i;
    struct kvm_regs regs;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    /* Set the registers based on QEMU's view of things */
    for (i = 0; i < 32; i++) {
        regs.gpr[i] = env->gpr[i];
    }

    regs.pc = env->pc;
    ret = kvm_vcpu_ioctl(cs, KVM_SET_REGS, &regs);
    if (ret < 0) {
        trace_kvm_failed_put_regs_core(strerror(errno));
    }

    return ret;
}

static int kvm_larch_getq(CPUState *cs, uint64_t reg_id,
                                 uint64_t *addr)
{
    struct kvm_one_reg csrreg = {
        .id = reg_id,
        .addr = (uintptr_t)addr
    };

    return kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &csrreg);
}

static int kvm_larch_putq(CPUState *cs, uint64_t reg_id,
                                 uint64_t *addr)
{
    struct kvm_one_reg csrreg = {
        .id = reg_id,
        .addr = (uintptr_t)addr
    };

    return kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &csrreg);
}

#define KVM_GET_ONE_UREG64(cs, ret, regidx, addr)                 \
    ({                                                            \
        err = kvm_larch_getq(cs, KVM_IOC_CSRID(regidx), addr);    \
        if (err < 0) {                                            \
            ret = err;                                            \
            trace_kvm_failed_get_csr(regidx, strerror(errno));    \
        }                                                         \
    })

#define KVM_PUT_ONE_UREG64(cs, ret, regidx, addr)                 \
    ({                                                            \
        err = kvm_larch_putq(cs, KVM_IOC_CSRID(regidx), addr);    \
        if (err < 0) {                                            \
            ret = err;                                            \
            trace_kvm_failed_put_csr(regidx, strerror(errno));    \
        }                                                         \
    })

static int kvm_loongarch_get_csr(CPUState *cs)
{
    int err, ret = 0;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_CRMD, &env->CSR_CRMD);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_PRMD, &env->CSR_PRMD);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_EUEN, &env->CSR_EUEN);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_MISC, &env->CSR_MISC);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_ECFG, &env->CSR_ECFG);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_ESTAT, &env->CSR_ESTAT);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_ERA, &env->CSR_ERA);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_BADV, &env->CSR_BADV);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_BADI, &env->CSR_BADI);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_EENTRY, &env->CSR_EENTRY);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBIDX, &env->CSR_TLBIDX);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBEHI, &env->CSR_TLBEHI);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBELO0, &env->CSR_TLBELO0);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBELO1, &env->CSR_TLBELO1);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_ASID, &env->CSR_ASID);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_PGDL, &env->CSR_PGDL);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_PGDH, &env->CSR_PGDH);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_PGD, &env->CSR_PGD);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_PWCL, &env->CSR_PWCL);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_PWCH, &env->CSR_PWCH);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_STLBPS, &env->CSR_STLBPS);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_RVACFG, &env->CSR_RVACFG);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_CPUID, &env->CSR_CPUID);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_PRCFG1, &env->CSR_PRCFG1);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_PRCFG2, &env->CSR_PRCFG2);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_PRCFG3, &env->CSR_PRCFG3);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(0), &env->CSR_SAVE[0]);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(1), &env->CSR_SAVE[1]);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(2), &env->CSR_SAVE[2]);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(3), &env->CSR_SAVE[3]);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(4), &env->CSR_SAVE[4]);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(5), &env->CSR_SAVE[5]);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(6), &env->CSR_SAVE[6]);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(7), &env->CSR_SAVE[7]);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TID, &env->CSR_TID);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_CNTC, &env->CSR_CNTC);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TICLR, &env->CSR_TICLR);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_LLBCTL, &env->CSR_LLBCTL);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_IMPCTL1, &env->CSR_IMPCTL1);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_IMPCTL2, &env->CSR_IMPCTL2);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRENTRY, &env->CSR_TLBRENTRY);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRBADV, &env->CSR_TLBRBADV);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRERA, &env->CSR_TLBRERA);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRSAVE, &env->CSR_TLBRSAVE);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRELO0, &env->CSR_TLBRELO0);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRELO1, &env->CSR_TLBRELO1);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBREHI, &env->CSR_TLBREHI);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRPRMD, &env->CSR_TLBRPRMD);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_DMW(0), &env->CSR_DMW[0]);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_DMW(1), &env->CSR_DMW[1]);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_DMW(2), &env->CSR_DMW[2]);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_DMW(3), &env->CSR_DMW[3]);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TVAL, &env->CSR_TVAL);
    KVM_GET_ONE_UREG64(cs, ret, LOONGARCH_CSR_TCFG, &env->CSR_TCFG);

    return ret;
}

static int kvm_loongarch_put_csr(CPUState *cs)
{
    int err, ret = 0;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_CRMD, &env->CSR_CRMD);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_PRMD, &env->CSR_PRMD);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_EUEN, &env->CSR_EUEN);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_MISC, &env->CSR_MISC);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_ECFG, &env->CSR_ECFG);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_ESTAT, &env->CSR_ESTAT);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_ERA, &env->CSR_ERA);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_BADV, &env->CSR_BADV);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_BADI, &env->CSR_BADI);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_EENTRY, &env->CSR_EENTRY);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBIDX, &env->CSR_TLBIDX);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBEHI, &env->CSR_TLBEHI);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBELO0, &env->CSR_TLBELO0);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBELO1, &env->CSR_TLBELO1);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_ASID, &env->CSR_ASID);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_PGDL, &env->CSR_PGDL);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_PGDH, &env->CSR_PGDH);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_PGD, &env->CSR_PGD);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_PWCL, &env->CSR_PWCL);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_PWCH, &env->CSR_PWCH);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_STLBPS, &env->CSR_STLBPS);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_RVACFG, &env->CSR_RVACFG);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_CPUID, &env->CSR_CPUID);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_PRCFG1, &env->CSR_PRCFG1);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_PRCFG2, &env->CSR_PRCFG2);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_PRCFG3, &env->CSR_PRCFG3);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(0), &env->CSR_SAVE[0]);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(1), &env->CSR_SAVE[1]);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(2), &env->CSR_SAVE[2]);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(3), &env->CSR_SAVE[3]);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(4), &env->CSR_SAVE[4]);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(5), &env->CSR_SAVE[5]);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(6), &env->CSR_SAVE[6]);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_SAVE(7), &env->CSR_SAVE[7]);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TID, &env->CSR_TID);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_CNTC, &env->CSR_CNTC);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TICLR, &env->CSR_TICLR);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_LLBCTL, &env->CSR_LLBCTL);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_IMPCTL1, &env->CSR_IMPCTL1);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_IMPCTL2, &env->CSR_IMPCTL2);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRENTRY, &env->CSR_TLBRENTRY);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRBADV, &env->CSR_TLBRBADV);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRERA, &env->CSR_TLBRERA);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRSAVE, &env->CSR_TLBRSAVE);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRELO0, &env->CSR_TLBRELO0);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRELO1, &env->CSR_TLBRELO1);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBREHI, &env->CSR_TLBREHI);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TLBRPRMD, &env->CSR_TLBRPRMD);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_DMW(0), &env->CSR_DMW[0]);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_DMW(1), &env->CSR_DMW[1]);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_DMW(2), &env->CSR_DMW[2]);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_DMW(3), &env->CSR_DMW[3]);
    /*
     * timer cfg must be put at last since it is used to enable
     * guest timer
     */
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TVAL, &env->CSR_TVAL);
    KVM_PUT_ONE_UREG64(cs, ret, LOONGARCH_CSR_TCFG, &env->CSR_TCFG);
    return ret;
}

static int kvm_loongarch_get_regs_fp(CPUState *cs)
{
    int ret, i;
    struct kvm_fpu fpu;

    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    ret = kvm_vcpu_ioctl(cs, KVM_GET_FPU, &fpu);
    if (ret < 0) {
        trace_kvm_failed_get_fpu(strerror(errno));
        return ret;
    }

    env->fcsr0 = fpu.fcsr;
    for (i = 0; i < 32; i++) {
        env->fpr[i].vreg.UD[0] = fpu.fpr[i].val64[0];
    }
    for (i = 0; i < 8; i++) {
        env->cf[i] = fpu.fcc & 0xFF;
        fpu.fcc = fpu.fcc >> 8;
    }

    return ret;
}

static int kvm_loongarch_put_regs_fp(CPUState *cs)
{
    int ret, i;
    struct kvm_fpu fpu;

    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    fpu.fcsr = env->fcsr0;
    fpu.fcc = 0;
    for (i = 0; i < 32; i++) {
        fpu.fpr[i].val64[0] = env->fpr[i].vreg.UD[0];
    }

    for (i = 0; i < 8; i++) {
        fpu.fcc |= env->cf[i] << (8 * i);
    }

    ret = kvm_vcpu_ioctl(cs, KVM_SET_FPU, &fpu);
    if (ret < 0) {
        trace_kvm_failed_put_fpu(strerror(errno));
    }

    return ret;
}

void kvm_arch_reset_vcpu(CPULoongArchState *env)
{
    env->mp_state = KVM_MP_STATE_RUNNABLE;
}

static int kvm_loongarch_get_mpstate(CPUState *cs)
{
    int ret = 0;
    struct kvm_mp_state mp_state;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    if (cap_has_mp_state) {
        ret = kvm_vcpu_ioctl(cs, KVM_GET_MP_STATE, &mp_state);
        if (ret) {
            trace_kvm_failed_get_mpstate(strerror(errno));
            return ret;
        }
        env->mp_state = mp_state.mp_state;
    }

    return ret;
}

static int kvm_loongarch_put_mpstate(CPUState *cs)
{
    int ret = 0;

    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    struct kvm_mp_state mp_state = {
        .mp_state = env->mp_state
    };

    if (cap_has_mp_state) {
        ret = kvm_vcpu_ioctl(cs, KVM_SET_MP_STATE, &mp_state);
        if (ret) {
            trace_kvm_failed_put_mpstate(strerror(errno));
        }
    }

    return ret;
}

static int kvm_loongarch_get_cpucfg(CPUState *cs)
{
    int i, ret = 0;
    uint64_t val;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    for (i = 0; i < 21; i++) {
        ret = kvm_larch_getq(cs, KVM_IOC_CPUCFG(i), &val);
        if (ret < 0) {
            trace_kvm_failed_get_cpucfg(strerror(errno));
        }
        env->cpucfg[i] = (uint32_t)val;
    }
    return ret;
}

static int kvm_loongarch_put_cpucfg(CPUState *cs)
{
    int i, ret = 0;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    uint64_t val;

    for (i = 0; i < 21; i++) {
        val = env->cpucfg[i];
        /* LSX and LASX and LBT are not supported in kvm now */
        if (i == 2) {
            val &= ~(BIT(R_CPUCFG2_LSX_SHIFT) | BIT(R_CPUCFG2_LASX_SHIFT));
            val &= ~(BIT(R_CPUCFG2_LBT_X86_SHIFT) | BIT(R_CPUCFG2_LBT_ARM_SHIFT) |
                     BIT(R_CPUCFG2_LBT_MIPS_SHIFT));
        }
        ret = kvm_larch_putq(cs, KVM_IOC_CPUCFG(i), &val);
        if (ret < 0) {
            trace_kvm_failed_put_cpucfg(strerror(errno));
        }
    }
    return ret;
}

int kvm_arch_get_registers(CPUState *cs)
{
    int ret;

    ret = kvm_loongarch_get_regs_core(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_get_csr(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_get_regs_fp(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_get_mpstate(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_get_cpucfg(cs);
    return ret;
}

int kvm_arch_put_registers(CPUState *cs, int level)
{
    int ret;

    ret = kvm_loongarch_put_regs_core(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_put_csr(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_put_regs_fp(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_put_mpstate(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_loongarch_put_cpucfg(cs);
    return ret;
}

static void kvm_loongarch_vm_stage_change(void *opaque, bool running,
                                          RunState state)
{
    int ret;
    CPUState *cs = opaque;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);

    if (running) {
        ret = kvm_larch_putq(cs, KVM_REG_LOONGARCH_COUNTER,
                             &cpu->kvm_state_counter);
        if (ret < 0) {
            trace_kvm_failed_put_counter(strerror(errno));
        }
    } else {
        ret = kvm_larch_getq(cs, KVM_REG_LOONGARCH_COUNTER,
                             &cpu->kvm_state_counter);
        if (ret < 0) {
            trace_kvm_failed_get_counter(strerror(errno));
        }
    }
}

int kvm_arch_init_vcpu(CPUState *cs)
{
    qemu_add_vm_change_state_handler(kvm_loongarch_vm_stage_change, cs);
    return 0;
}

int kvm_arch_destroy_vcpu(CPUState *cs)
{
    return 0;
}

unsigned long kvm_arch_vcpu_id(CPUState *cs)
{
    return cs->cpu_index;
}

int kvm_arch_release_virq_post(int virq)
{
    return 0;
}

int kvm_arch_msi_data_to_gsi(uint32_t data)
{
    abort();
}

int kvm_arch_fixup_msi_route(struct kvm_irq_routing_entry *route,
                             uint64_t address, uint32_t data, PCIDevice *dev)
{
    return 0;
}

int kvm_arch_add_msi_route_post(struct kvm_irq_routing_entry *route,
                                int vector, PCIDevice *dev)
{
    return 0;
}

void kvm_arch_init_irq_routing(KVMState *s)
{
}

int kvm_arch_get_default_type(MachineState *ms)
{
    return 0;
}

int kvm_arch_init(MachineState *ms, KVMState *s)
{
    cap_has_mp_state = kvm_check_extension(s, KVM_CAP_MP_STATE);
    return 0;
}

int kvm_arch_irqchip_create(KVMState *s)
{
    return 0;
}

void kvm_arch_pre_run(CPUState *cs, struct kvm_run *run)
{
}

MemTxAttrs kvm_arch_post_run(CPUState *cs, struct kvm_run *run)
{
    return MEMTXATTRS_UNSPECIFIED;
}

int kvm_arch_process_async_events(CPUState *cs)
{
    return cs->halted;
}

bool kvm_arch_stop_on_emulation_error(CPUState *cs)
{
    return true;
}

bool kvm_arch_cpu_check_are_resettable(void)
{
    return true;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    int ret = 0;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    MemTxAttrs attrs = {};

    attrs.requester_id = env_cpu(env)->cpu_index;

    trace_kvm_arch_handle_exit(run->exit_reason);
    switch (run->exit_reason) {
    case KVM_EXIT_LOONGARCH_IOCSR:
        address_space_rw(&env->address_space_iocsr,
                         run->iocsr_io.phys_addr,
                         attrs,
                         run->iocsr_io.data,
                         run->iocsr_io.len,
                         run->iocsr_io.is_write);
        break;
    default:
        ret = -1;
        warn_report("KVM: unknown exit reason %d", run->exit_reason);
        break;
    }
    return ret;
}

int kvm_loongarch_set_interrupt(LoongArchCPU *cpu, int irq, int level)
{
    struct kvm_interrupt intr;
    CPUState *cs = CPU(cpu);

    if (level) {
        intr.irq = irq;
    } else {
        intr.irq = -irq;
    }

    trace_kvm_set_intr(irq, level);
    return kvm_vcpu_ioctl(cs, KVM_INTERRUPT, &intr);
}

void kvm_arch_accel_class_init(ObjectClass *oc)
{
}
