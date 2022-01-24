#include <stdio.h>
#include <stdarg.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

void qemu_logf(const char *str, ...)
{
    char message[1024];
    va_list args;
    va_start(args, str);
    vsnprintf(message, 1023, str, args);

    qemu_plugin_outs(message);

    va_end(args);
}

void before_insn_cb(unsigned int cpu_index, void *udata)
{
    uint64_t pc = (uint64_t)udata;
    qemu_logf("Executing PC: 0x%" PRIx64 "\n", pc);
}

static void mem_cb(unsigned int cpu_index, qemu_plugin_meminfo_t meminfo, uint64_t va, void *udata)
{
    uint64_t pc = (uint64_t)udata;
    qemu_logf("PC 0x%" PRIx64 " accessed memory at 0x%" PRIx64 "\n", pc, va);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);

    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t pc = qemu_plugin_insn_vaddr(insn);

        qemu_plugin_register_vcpu_insn_exec_cb(insn, before_insn_cb, QEMU_PLUGIN_CB_R_REGS, (void *)pc);
        qemu_plugin_register_vcpu_mem_cb(insn, mem_cb, QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_RW, (void*)pc);
    }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    return 0;
}
