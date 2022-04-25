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
#include "tcg/tcg-ldst.h"

uint64_t helper_iocsrrd_b(CPULoongArchState *env, target_ulong r_addr)
{
    return address_space_ldub(&env->address_space_iocsr, r_addr,
                              MEMTXATTRS_UNSPECIFIED, NULL);
}

uint64_t helper_iocsrrd_h(CPULoongArchState *env, target_ulong r_addr)
{
    return address_space_lduw(&env->address_space_iocsr, r_addr,
                              MEMTXATTRS_UNSPECIFIED, NULL);
}

uint64_t helper_iocsrrd_w(CPULoongArchState *env, target_ulong r_addr)
{
    return address_space_ldl(&env->address_space_iocsr, r_addr,
                             MEMTXATTRS_UNSPECIFIED, NULL);
}

uint64_t helper_iocsrrd_d(CPULoongArchState *env, target_ulong r_addr)
{
    return address_space_ldq(&env->address_space_iocsr, r_addr,
                             MEMTXATTRS_UNSPECIFIED, NULL);
}

static int get_ipi_data(target_ulong val)
{
    int i, mask, data;

    data = val >> 32;
    mask = (val >> 27) & 0xf;

    for (i = 0; i < 4; i++) {
        if ((mask >> i) & 1) {
            data &= ~(0xff << (i * 8));
        }
    }
    return data;
}

static void check_ipi_send(CPULoongArchState *env, target_ulong val)
{
    int cpuid;
    target_ulong data;

    cpuid = (val >> 16) & 0x3ff;
    /* IPI status vector */
    data = 1 << (val & 0x1f);

    qemu_mutex_lock_iothread();
    CPUState *cs = qemu_get_cpu(cpuid);
    env = cs->env_ptr;
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    loongarch_cpu_set_irq(cpu, IRQ_IPI, 1);
    qemu_mutex_unlock_iothread();

    address_space_stl(&env->address_space_iocsr, 0x1008,
                      data, MEMTXATTRS_UNSPECIFIED, NULL);
}

static void check_mail_send(CPULoongArchState *env, target_ulong w_addr,
                     target_ulong val)
{
    int cpuid;
    uint32_t data;

    cpuid = (val >> 16) & 0x3ff;
    w_addr = 0x1020 + (val & 0x1c);
    env = (qemu_get_cpu(cpuid))->env_ptr;
    data = get_ipi_data(val);

    address_space_stl(&env->address_space_iocsr, w_addr,
                      data, MEMTXATTRS_UNSPECIFIED, NULL);
}

static void check_any_send(CPULoongArchState *env, target_ulong w_addr,
                    target_ulong val)
{
    int cpuid;
    uint32_t data;

    cpuid = (val >> 16) & 0x3ff;
    w_addr = val & 0xffff;
    env = (qemu_get_cpu(cpuid))->env_ptr;
    data = get_ipi_data(val);

    address_space_stl(&env->address_space_iocsr, w_addr,
                      data, MEMTXATTRS_UNSPECIFIED, NULL);
}

static bool check_iocsrwr(CPULoongArchState *env, target_ulong w_addr,
                   target_ulong val)
{
    bool ret = true;

    /*
     * For IPI send, Mailbox send and ANY send, adjust the addr and
     * val accordingly. The IOCSR writes are turned to different
     * MMIO writes respectively
     */
    switch (w_addr) {
    case 0x1040: /* IPI send */
        check_ipi_send(env, val);
        ret = false;
        break;
    case 0x1048: /* Mail send */
        check_mail_send(env, w_addr, val);
        ret = false;
        break;
    case 0x1158: /* ANY send */
        check_any_send(env, w_addr, val);
        ret = false;
        break;
    default:
        break;
    }
    return ret;
}

void helper_iocsrwr_b(CPULoongArchState *env, target_ulong w_addr,
                      target_ulong val)
{
    if (!check_iocsrwr(env, w_addr, val)) {
        return;
    }
    address_space_stb(&env->address_space_iocsr, w_addr,
                      val, MEMTXATTRS_UNSPECIFIED, NULL);
}

void helper_iocsrwr_h(CPULoongArchState *env, target_ulong w_addr,
                      target_ulong val)
{
    if (!check_iocsrwr(env, w_addr, val)) {
        return;
    }
    address_space_stw(&env->address_space_iocsr, w_addr,
                      val, MEMTXATTRS_UNSPECIFIED, NULL);
}

void helper_iocsrwr_w(CPULoongArchState *env, target_ulong w_addr,
                      target_ulong val)
{
    if (!check_iocsrwr(env, w_addr, val)) {
        return;
    }
    address_space_stl(&env->address_space_iocsr, w_addr,
                      val, MEMTXATTRS_UNSPECIFIED, NULL);
}

void helper_iocsrwr_d(CPULoongArchState *env, target_ulong w_addr,
                      target_ulong val)
{
    if (!check_iocsrwr(env, w_addr, val)) {
        return;
    }
    address_space_stq(&env->address_space_iocsr, w_addr,
                      val, MEMTXATTRS_UNSPECIFIED, NULL);
}
