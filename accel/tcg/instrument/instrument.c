#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "tcg/tcg.h"
#include "tcg/tcg-op.h"
#include "exec/exec-all.h"
#include "exec/log.h"
#include "exec/translator.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "qemu/log.h"

#include "instrument.h"

//#define QI_ALL
#define QI_SYSCALL

bool qi_needs_before_insn(DisasContextBase *db, CPUState *cpu)
{
#ifdef QI_ALL
    /* instrument all the instructions */
    return true;
#endif
#ifdef QI_SYSCALL
    /* instrument only system calls */
#ifdef TARGET_I386
    uint8_t code = 0;
    // int 80h is processed by exception handlers
    if (!cpu_memory_rw_debug(cpu, db->pc_next, &code, 1, false)
        && code == 0x0f) {
        if (cpu_memory_rw_debug(cpu, db->pc_next + 1, &code, 1, false)) {
            return false;
        }
        if (code == 0x34) {
            /* sysenter */
            return true;
        }
        if (code == 0x35) {
            /* sysexit */
            return true;
        }
    }
#endif    
    return false;
#endif
}

void qi_instrument_before_insn(DisasContextBase *db, CPUState *cpu)
{
    TCGv t_pc = tcg_const_tl(db->pc_next);
    TCGv_ptr t_cpu= tcg_const_ptr(cpu);
    gen_helper_before_insn(t_pc, t_cpu);
    tcg_temp_free(t_pc);
    tcg_temp_free_ptr(t_cpu);
}

void helper_before_insn(target_ulong pc, void *cpu)
{
#ifdef QI_ALL
    /* log all the executed instructions */
    qemu_log("executing %"PRIx64"\n", (uint64_t)pc);
#endif
#ifdef QI_SYSCALL
    uint8_t code = 0;
    cpu_memory_rw_debug(cpu, pc + 1, &code, 1, false);
#ifdef TARGET_I386
    CPUArchState *env = ((CPUState*)cpu)->env_ptr;
    /* log system calls */
    if (code == 0x34) {
        qemu_log("syscall %x\n", (uint32_t)env->regs[R_EAX]);
    } else if (code == 0x35) {
        qemu_log("sysexit %x\n", (uint32_t)env->regs[R_EAX]);
    }
#endif
#endif
}

void qi_init(void)
{
#include "exec/helper-register.h"
}
