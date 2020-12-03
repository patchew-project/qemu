/*
 * RISC-V implementation of KVM hooks
 *
 * Copyright (c) 2020 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include <linux/kvm.h>

#include "qemu-common.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "sysemu/kvm_int.h"
#include "cpu.h"
#include "trace.h"
#include "hw/pci/pci.h"
#include "exec/memattrs.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "hw/loader.h"

static __u64 kvm_riscv_reg_id(__u64 type, __u64 idx)
{
    __u64 id = KVM_REG_RISCV | type | idx;

#if defined(TARGET_RISCV32)
    id |= KVM_REG_SIZE_U32;
#elif defined(TARGET_RISCV64)
    id |= KVM_REG_SIZE_U64;
#endif
    return id;
}

#define RISCV_CORE_REG(name)  kvm_riscv_reg_id(KVM_REG_RISCV_CORE, \
                 KVM_REG_RISCV_CORE_REG(name))

#define RISCV_CSR_REG(name)  kvm_riscv_reg_id(KVM_REG_RISCV_CSR, \
                 KVM_REG_RISCV_CSR_REG(name))

#define RISCV_FP_F_REG(idx)  kvm_riscv_reg_id(KVM_REG_RISCV_FP_F, idx)

#define RISCV_FP_D_REG(idx)  kvm_riscv_reg_id(KVM_REG_RISCV_FP_D, idx)

static int kvm_riscv_get_regs_core(CPUState *cs)
{
    int ret = 0;
    int i;
    target_ulong reg;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    ret = kvm_get_one_reg(cs, RISCV_CORE_REG(regs.pc), &reg);
    if (ret) {
        return ret;
    }
    env->pc = reg;

    for (i = 1; i < 32; i++) {
        __u64 id = kvm_riscv_reg_id(KVM_REG_RISCV_CORE, i);
        ret = kvm_get_one_reg(cs, id, &reg);
        if (ret) {
            return ret;
        }
        env->gpr[i] = reg;
    }

    return ret;
}

static int kvm_riscv_put_regs_core(CPUState *cs)
{
    int ret = 0;
    int i;
    target_ulong reg;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    reg = env->pc;
    ret = kvm_set_one_reg(cs, RISCV_CORE_REG(regs.pc), &reg);
    if (ret) {
        return ret;
    }

    for (i = 1; i < 32; i++) {
        __u64 id = kvm_riscv_reg_id(KVM_REG_RISCV_CORE, i);
        reg = env->gpr[i];
        ret = kvm_set_one_reg(cs, id, &reg);
        if (ret) {
            return ret;
        }
    }

    return ret;
}

static int kvm_riscv_get_regs_csr(CPUState *cs)
{
    int ret = 0;
    target_ulong reg;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    ret = kvm_get_one_reg(cs, RISCV_CSR_REG(sstatus), &reg);
    if (ret) {
        return ret;
    }
    env->mstatus = reg;

    ret = kvm_get_one_reg(cs, RISCV_CSR_REG(sie), &reg);
    if (ret) {
        return ret;
    }
    env->mie = reg;

    ret = kvm_get_one_reg(cs, RISCV_CSR_REG(stvec), &reg);
    if (ret) {
        return ret;
    }
    env->stvec = reg;

    ret = kvm_get_one_reg(cs, RISCV_CSR_REG(sscratch), &reg);
    if (ret) {
        return ret;
    }
    env->sscratch = reg;

    ret = kvm_get_one_reg(cs, RISCV_CSR_REG(sepc), &reg);
    if (ret) {
        return ret;
    }
    env->sepc = reg;

    ret = kvm_get_one_reg(cs, RISCV_CSR_REG(scause), &reg);
    if (ret) {
        return ret;
    }
    env->scause = reg;

    ret = kvm_get_one_reg(cs, RISCV_CSR_REG(stval), &reg);
    if (ret) {
        return ret;
    }
    env->sbadaddr = reg;

    ret = kvm_get_one_reg(cs, RISCV_CSR_REG(sip), &reg);
    if (ret) {
        return ret;
    }
    env->mip = reg;

    ret = kvm_get_one_reg(cs, RISCV_CSR_REG(satp), &reg);
    if (ret) {
        return ret;
    }
    env->satp = reg;

    return ret;
}

static int kvm_riscv_put_regs_csr(CPUState *cs)
{
    int ret = 0;
    target_ulong reg;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    reg = env->mstatus;
    ret = kvm_set_one_reg(cs, RISCV_CSR_REG(sstatus), &reg);
    if (ret) {
        return ret;
    }

    reg = env->mie;
    ret = kvm_set_one_reg(cs, RISCV_CSR_REG(sie), &reg);
    if (ret) {
        return ret;
    }

    reg = env->stvec;
    ret = kvm_set_one_reg(cs, RISCV_CSR_REG(stvec), &reg);
    if (ret) {
        return ret;
    }

    reg = env->sscratch;
    ret = kvm_set_one_reg(cs, RISCV_CSR_REG(sscratch), &reg);
    if (ret) {
        return ret;
    }

    reg = env->sepc;
    ret = kvm_set_one_reg(cs, RISCV_CSR_REG(sepc), &reg);
    if (ret) {
        return ret;
    }

    reg = env->scause;
    ret = kvm_set_one_reg(cs, RISCV_CSR_REG(scause), &reg);
    if (ret) {
        return ret;
    }

    reg = env->sbadaddr;
    ret = kvm_set_one_reg(cs, RISCV_CSR_REG(stval), &reg);
    if (ret) {
        return ret;
    }

    reg = env->mip;
    ret = kvm_set_one_reg(cs, RISCV_CSR_REG(sip), &reg);
    if (ret) {
        return ret;
    }

    reg = env->satp;
    ret = kvm_set_one_reg(cs, RISCV_CSR_REG(satp), &reg);
    if (ret) {
        return ret;
    }

    return ret;
}


static int kvm_riscv_get_regs_fp(CPUState *cs)
{
    int ret = 0;
    int i;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    if (riscv_has_ext(env, RVD)) {
        uint64_t reg;
        for (i = 0; i < 32; i++) {
            ret = kvm_get_one_reg(cs, RISCV_FP_D_REG(i), &reg);
            if (ret) {
                return ret;
            }
            env->fpr[i] = reg;
        }
        return ret;
    }

    if (riscv_has_ext(env, RVF)) {
        uint32_t reg;
        for (i = 0; i < 32; i++) {
            ret = kvm_get_one_reg(cs, RISCV_FP_F_REG(i), &reg);
            if (ret) {
                return ret;
            }
            env->fpr[i] = reg;
        }
        return ret;
    }

    return ret;
}

static int kvm_riscv_put_regs_fp(CPUState *cs)
{
    int ret = 0;
    int i;
    CPURISCVState *env = &RISCV_CPU(cs)->env;

    if (riscv_has_ext(env, RVD)) {
        uint64_t reg;
        for (i = 0; i < 32; i++) {
            reg = env->fpr[i];
            ret = kvm_set_one_reg(cs, RISCV_FP_D_REG(i), &reg);
            if (ret) {
                return ret;
            }
        }
        return ret;
    }

    if (riscv_has_ext(env, RVF)) {
        uint32_t reg;
        for (i = 0; i < 32; i++) {
            reg = env->fpr[i];
            ret = kvm_set_one_reg(cs, RISCV_FP_F_REG(i), &reg);
            if (ret) {
                return ret;
            }
        }
        return ret;
    }

    return ret;
}


const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

int kvm_arch_get_registers(CPUState *cs)
{
    int ret = 0;

    ret = kvm_riscv_get_regs_core(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_riscv_get_regs_csr(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_riscv_get_regs_fp(cs);
    if (ret) {
        return ret;
    }

    return ret;
}

int kvm_arch_put_registers(CPUState *cs, int level)
{
    int ret = 0;

    ret = kvm_riscv_put_regs_core(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_riscv_put_regs_csr(cs);
    if (ret) {
        return ret;
    }

    ret = kvm_riscv_put_regs_fp(cs);
    if (ret) {
        return ret;
    }

    return ret;
}

int kvm_arch_release_virq_post(int virq)
{
    return 0;
}

int kvm_arch_fixup_msi_route(struct kvm_irq_routing_entry *route,
                             uint64_t address, uint32_t data, PCIDevice *dev)
{
    return 0;
}

int kvm_arch_destroy_vcpu(CPUState *cs)
{
    return 0;
}

unsigned long kvm_arch_vcpu_id(CPUState *cpu)
{
    return cpu->cpu_index;
}

void kvm_arch_init_irq_routing(KVMState *s)
{
}

int kvm_arch_init_vcpu(CPUState *cs)
{
    int ret = 0;
    target_ulong isa;
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    __u64 id;

    id = kvm_riscv_reg_id(KVM_REG_RISCV_CONFIG, KVM_REG_RISCV_CONFIG_REG(isa));
    ret = kvm_get_one_reg(cs, id, &isa);
    if (ret) {
        return ret;
    }
    env->misa = isa;

    return ret;
}

int kvm_arch_msi_data_to_gsi(uint32_t data)
{
    abort();
}

int kvm_arch_add_msi_route_post(struct kvm_irq_routing_entry *route,
                                int vector, PCIDevice *dev)
{
    return 0;
}

int kvm_arch_init(MachineState *ms, KVMState *s)
{
    return 0;
}

int kvm_arch_irqchip_create(KVMState *s)
{
    return 0;
}

int kvm_arch_process_async_events(CPUState *cs)
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

bool kvm_arch_stop_on_emulation_error(CPUState *cs)
{
    return true;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    return 0;
}
