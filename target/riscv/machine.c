#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "migration/cpu.h"

static bool pmp_needed(void *opaque)
{
    RISCVCPU *cpu = opaque;
    CPURISCVState *env = &cpu->env;

    return riscv_feature(env, RISCV_FEATURE_PMP);
}

static const VMStateDescription vmstate_pmp_entry = {
    .name = "cpu/pmp/entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL(addr_reg, pmp_entry_t),
        VMSTATE_UINT8(cfg_reg, pmp_entry_t),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pmp_addr = {
    .name = "cpu/pmp/addr",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL(sa, pmp_addr_t),
        VMSTATE_UINTTL(ea, pmp_addr_t),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pmp = {
    .name = "cpu/pmp",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pmp_needed,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(env.pmp_state.pmp, RISCVCPU, MAX_RISCV_PMPS,
                             0, vmstate_pmp_entry, pmp_entry_t),
        VMSTATE_STRUCT_ARRAY(env.pmp_state.addr, RISCVCPU, MAX_RISCV_PMPS,
                             0, vmstate_pmp_addr, pmp_addr_t),
        VMSTATE_UINT32(env.pmp_state.num_rules, RISCVCPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool hyper_needed(void *opaque)
{
    RISCVCPU *cpu = opaque;
    CPURISCVState *env = &cpu->env;

    return riscv_has_ext(env, RVH);
}

static const VMStateDescription vmstate_hyper = {
    .name = "cpu/hyper",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyper_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL(env.hstatus, RISCVCPU),
        VMSTATE_UINTTL(env.hedeleg, RISCVCPU),
        VMSTATE_UINTTL(env.hideleg, RISCVCPU),
        VMSTATE_UINTTL(env.hcounteren, RISCVCPU),
        VMSTATE_UINTTL(env.htval, RISCVCPU),
        VMSTATE_UINTTL(env.htinst, RISCVCPU),
        VMSTATE_UINTTL(env.hgatp, RISCVCPU),
        VMSTATE_UINT64(env.htimedelta, RISCVCPU),

        VMSTATE_UINTTL(env.vsstatus, RISCVCPU),
        VMSTATE_UINTTL(env.vstvec, RISCVCPU),
        VMSTATE_UINTTL(env.vsscratch, RISCVCPU),
        VMSTATE_UINTTL(env.vsepc, RISCVCPU),
        VMSTATE_UINTTL(env.vscause, RISCVCPU),
        VMSTATE_UINTTL(env.vstval, RISCVCPU),
        VMSTATE_UINTTL(env.vsatp, RISCVCPU),

        VMSTATE_UINTTL(env.mtval2, RISCVCPU),
        VMSTATE_UINTTL(env.mtinst, RISCVCPU),

        VMSTATE_UINTTL(env.stvec_hs, RISCVCPU),
        VMSTATE_UINTTL(env.sscratch_hs, RISCVCPU),
        VMSTATE_UINTTL(env.sepc_hs, RISCVCPU),
        VMSTATE_UINTTL(env.scause_hs, RISCVCPU),
        VMSTATE_UINTTL(env.stval_hs, RISCVCPU),
        VMSTATE_UINTTL(env.satp_hs, RISCVCPU),
        VMSTATE_UINTTL(env.mstatus_hs, RISCVCPU),

#ifdef TARGET_RISCV32
        VMSTATE_UINTTL(env.vsstatush, RISCVCPU),
        VMSTATE_UINTTL(env.mstatush_hs, RISCVCPU),
#endif
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_riscv_cpu = {
    .name = "cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL_ARRAY(env.gpr, RISCVCPU, 32),
        VMSTATE_UINT64_ARRAY(env.fpr, RISCVCPU, 32),
        VMSTATE_UINTTL(env.pc, RISCVCPU),
        VMSTATE_UINTTL(env.load_res, RISCVCPU),
        VMSTATE_UINTTL(env.load_val, RISCVCPU),
        VMSTATE_UINTTL(env.frm, RISCVCPU),
        VMSTATE_UINTTL(env.badaddr, RISCVCPU),
        VMSTATE_UINTTL(env.guest_phys_fault_addr, RISCVCPU),
        VMSTATE_UINTTL(env.priv_ver, RISCVCPU),
        VMSTATE_UINTTL(env.vext_ver, RISCVCPU),
        VMSTATE_UINTTL(env.misa, RISCVCPU),
        VMSTATE_UINTTL(env.misa_mask, RISCVCPU),
        VMSTATE_UINT32(env.features, RISCVCPU),
        VMSTATE_UINTTL(env.priv, RISCVCPU),
        VMSTATE_UINTTL(env.virt, RISCVCPU),
        VMSTATE_UINTTL(env.resetvec, RISCVCPU),
        VMSTATE_UINTTL(env.mhartid, RISCVCPU),
        VMSTATE_UINTTL(env.mstatus, RISCVCPU),
        VMSTATE_UINTTL(env.mip, RISCVCPU),
        VMSTATE_UINT32(env.miclaim, RISCVCPU),
        VMSTATE_UINTTL(env.mie, RISCVCPU),
        VMSTATE_UINTTL(env.mideleg, RISCVCPU),
        VMSTATE_UINTTL(env.sptbr, RISCVCPU),
        VMSTATE_UINTTL(env.satp, RISCVCPU),
        VMSTATE_UINTTL(env.sbadaddr, RISCVCPU),
        VMSTATE_UINTTL(env.mbadaddr, RISCVCPU),
        VMSTATE_UINTTL(env.medeleg, RISCVCPU),
        VMSTATE_UINTTL(env.stvec, RISCVCPU),
        VMSTATE_UINTTL(env.sepc, RISCVCPU),
        VMSTATE_UINTTL(env.scause, RISCVCPU),
        VMSTATE_UINTTL(env.mtvec, RISCVCPU),
        VMSTATE_UINTTL(env.mepc, RISCVCPU),
        VMSTATE_UINTTL(env.mcause, RISCVCPU),
        VMSTATE_UINTTL(env.mtval, RISCVCPU),
        VMSTATE_UINTTL(env.scounteren, RISCVCPU),
        VMSTATE_UINTTL(env.mcounteren, RISCVCPU),
        VMSTATE_UINTTL(env.sscratch, RISCVCPU),
        VMSTATE_UINTTL(env.mscratch, RISCVCPU),
        VMSTATE_UINT64(env.mfromhost, RISCVCPU),
        VMSTATE_UINT64(env.mtohost, RISCVCPU),
        VMSTATE_UINT64(env.timecmp, RISCVCPU),

#ifdef TARGET_RISCV32
        VMSTATE_UINTTL(env.mstatush, RISCVCPU),
#endif
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_pmp,
        &vmstate_hyper,
        NULL
    }
};
