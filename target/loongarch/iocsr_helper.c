/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * Helpers for IOCSR reads/writes
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "internals.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "hw/irq.h"
#include "cpu-csr.h"
#include "hw/loongarch/loongarch.h"
#include "tcg/tcg-ldst.h"

uint64_t helper_iocsr_read(CPULoongArchState *env, target_ulong r_addr,
                           uint32_t size)
{
    int cpuid = env_cpu(env)->cpu_index;
    CPUState  *cs = qemu_get_cpu(cpuid);
    env = cs->env_ptr;
    uint64_t ret = 0;

    /*
     * Adjust the per core address such as 0x10xx(IPI)/0x18xx(EXTIOI)
     */
    if (((r_addr & 0xff00) == 0x1000) || ((r_addr & 0xff00) == 0x1800)) {
        r_addr = r_addr + ((target_ulong)(cpuid & 0x3) << 8);
    }

    switch (size) {
    case 1:
        ret = address_space_ldub(&env->address_space_iocsr, r_addr,
                                 MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 2:
        ret = address_space_lduw(&env->address_space_iocsr, r_addr,
                                 MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 4:
        ret = address_space_ldl(&env->address_space_iocsr, r_addr,
                                MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 8:
        ret = address_space_ldq(&env->address_space_iocsr, r_addr,
                                MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    default:
        g_assert_not_reached();
    }

    return ret;
}

void helper_iocsr_write(CPULoongArchState *env, target_ulong w_addr,
                        target_ulong val, uint32_t size)
{
    int cpuid = env_cpu(env)->cpu_index;
    CPUState *cs = qemu_get_cpu(cpuid);
    int mask, i;
    env = cs->env_ptr;

    /*
     * For IPI send, Mailbox send and ANY send, adjust the addr and
     * val accordingly. The IOCSR writes are turned to different
     * MMIO writes respectively
     */
    switch (w_addr) {
    case 0x1040: /* IPI send */
        cpuid = (val >> 16) & 0x3ff;
        val = 1UL << (val & 0x1f);
        if (val) {
            qemu_mutex_lock_iothread();
            cs = qemu_get_cpu(cpuid);
            env = cs->env_ptr;
            LoongArchCPU *cpu = LOONGARCH_CPU(cs);
            loongarch_cpu_set_irq(cpu, IRQ_IPI, 1);
            qemu_mutex_unlock_iothread();
        }
        break;
    case 0x1048: /* Mail Send */
        cpuid = (val >> 16) & 0x3ff;
        w_addr = 0x1020 + (val & 0x1c);
        val = val >> 32;
        mask = (val >> 27) & 0xf;
        size = 4;
        env = (qemu_get_cpu(cpuid))->env_ptr;
        break;
    case 0x1158: /* ANY send */
        cpuid = (val >> 16) & 0x3ff;
        w_addr = val & 0xffff;
        val = val >> 32;
        mask = (val >> 27) & 0xf;
        size = 1;
        env = (qemu_get_cpu(cpuid))->env_ptr;

        for (i = 0; i < 4; i++) {
            if (!((mask >> i) & 1)) {
                address_space_stb(&env->address_space_iocsr, w_addr,
                                  val, MEMTXATTRS_UNSPECIFIED, NULL);
            }
            w_addr = w_addr + 1;
            val = val >> 8;
        }
        return;
    default:
       break;
    }

    if (((w_addr & 0xff00) == 0x1000) || ((w_addr & 0xff00) == 0x1800)) {
        w_addr = w_addr + ((target_ulong)(cpuid & 0x3) << 8);
    }

    switch (size) {
    case 1:
        address_space_stb(&env->address_space_iocsr, w_addr,
                          val, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 2:
        address_space_stw(&env->address_space_iocsr, w_addr,
                          val, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 4:
        address_space_stl(&env->address_space_iocsr, w_addr,
                          val, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 8:
        address_space_stq(&env->address_space_iocsr, w_addr,
                          val, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    default:
        g_assert_not_reached();
    }
}
