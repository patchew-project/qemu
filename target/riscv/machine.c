#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "migration/cpu.h"

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
    }
};
