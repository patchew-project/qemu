/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2026, Florian Hofhammer <florian.hofhammer@epfl.ch>
 */
#include <assert.h>
#include <glib.h>
#include <inttypes.h>
#include <unistd.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t source_pc;
static uint64_t target_pc;
static uint64_t target_vaddr;

static void vcpu_syscall(qemu_plugin_id_t id, unsigned int vcpu_index,
                         int64_t num, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4, uint64_t a5,
                         uint64_t a6, uint64_t a7, uint64_t a8)
{
    if (num == 4096) {
        qemu_plugin_outs("Marker syscall detected, jump to clean return\n");
        qemu_plugin_set_pc(a1);
    }
}

static bool vcpu_syscall_filter(qemu_plugin_id_t id, unsigned int vcpu_index,
                                int64_t num, uint64_t a1, uint64_t a2,
                                uint64_t a3, uint64_t a4, uint64_t a5,
                                uint64_t a6, uint64_t a7, uint64_t a8,
                                uint64_t *sysret)
{
    if (num == 4095) {
        qemu_plugin_outs("Communication syscall detected, set target_pc / "
                         "target_vaddr\n");
        source_pc = a1;
        target_pc = a2;
        target_vaddr = a3;
        if (source_pc >> 63 || target_pc >> 63 || target_vaddr >> 63) {
            /*
             * Some architectures (e.g., m68k) use 32-bit addresses with the
             * top bit set, which causes them to get sign-extended somewhere in
             * the chain to this callback. We mask the top bits off here to get
             * the actual addresses.
             */
            qemu_plugin_outs("High bit in addresses detected: possible sign "
                             "extension in syscall, masking off top bits\n");
            source_pc &= UINT32_MAX;
            target_pc &= UINT32_MAX;
            target_vaddr &= UINT32_MAX;
        }
        *sysret = 0;
        return true;
    }
    return false;
}

static void vcpu_insn_exec(unsigned int vcpu_index, void *userdata)
{
    uint64_t vaddr = (uint64_t)userdata;
    if (vaddr == source_pc) {
        g_assert(target_pc != 0);
        g_assert(target_vaddr == 0);

        qemu_plugin_outs("Marker instruction detected, jump to clean return\n");
        qemu_plugin_set_pc(target_pc);
    }
}

static void vcpu_mem_access(unsigned int vcpu_index,
                            qemu_plugin_meminfo_t info,
                            uint64_t vaddr, void *userdata)
{
    if (vaddr != 0 && vaddr == target_vaddr) {
        g_assert(source_pc == 0);
        g_assert(target_pc != 0);
        qemu_plugin_mem_value val = qemu_plugin_mem_get_value(info);
        /* target_vaddr points to our volatile guard ==> should always be 1 */
        g_assert(val.type == QEMU_PLUGIN_MEM_VALUE_U32);
        g_assert(val.data.u32 == 1);

        qemu_plugin_outs("Marker mem access detected, jump to clean return\n");
        qemu_plugin_set_pc(target_pc);
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t insns = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < insns; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t insn_vaddr = qemu_plugin_insn_vaddr(insn);
        /*
         * Note: we cannot only register the callbacks if the instruction is
         * in one of the functions of interest, because symbol lookup for
         * filtering does not work for all architectures (e.g., ppc64).
         */
        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_insn_exec,
                                               QEMU_PLUGIN_CB_RW_REGS_PC,
                                               (void *)insn_vaddr);
        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem_access,
                                         QEMU_PLUGIN_CB_RW_REGS_PC,
                                         QEMU_PLUGIN_MEM_R, NULL);
    }
}


QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{

    qemu_plugin_register_vcpu_syscall_cb(id, vcpu_syscall);
    qemu_plugin_register_vcpu_syscall_filter_cb(id, vcpu_syscall_filter);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    return 0;
}
