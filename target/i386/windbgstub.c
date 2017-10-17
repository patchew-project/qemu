/*
 * windbgstub.c
 *
 * Copyright (c) 2010-2017 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "exec/windbgstub-utils.h"

#ifdef TARGET_X86_64
# define OFFSET_SELF_PCR         0x18
# define OFFSET_VERS             0x108
# define OFFSET_KPRCB            0x20
# define OFFSET_KPRCB_CURRTHREAD 0x8
#else
# define OFFSET_SELF_PCR         0x1C
# define OFFSET_VERS             0x34
# define OFFSET_KPRCB            0x20
# define OFFSET_KPRCB_CURRTHREAD 0x4
#endif

bool windbg_on_load(void)
{
    CPUState *cpu = qemu_get_cpu(0);
    CPUArchState *env = cpu->env_ptr;
    InitedAddr *KPCR = windbg_get_KPCR();
    InitedAddr *version = windbg_get_version();

    if (!KPCR->is_init) {

 #ifdef TARGET_X86_64
        KPCR->addr = env->segs[R_GS].base;
 #else
        KPCR->addr = env->segs[R_FS].base;
 #endif

        static target_ulong prev_KPCR;
        if (!KPCR->addr || prev_KPCR == KPCR->addr) {
            return false;
        }
        prev_KPCR = KPCR->addr;

        if (KPCR->addr != READ_VMEM(cpu, KPCR->addr + OFFSET_SELF_PCR,
                                    target_ulong)) {
            return false;
        }

        KPCR->is_init = true;
    }

    if (!version->is_init && KPCR->is_init) {
        version->addr = READ_VMEM(cpu, KPCR->addr + OFFSET_VERS,
                                  target_ulong);
        if (!version->addr) {
            return false;
        }
        version->is_init = true;
    }

    WINDBG_DEBUG("windbg_on_load: KPCR " FMT_ADDR, KPCR->addr);
    WINDBG_DEBUG("windbg_on_load: version " FMT_ADDR, version->addr);

    return true;
}

static void kd_init_state_change(CPUState *cpu,
                                 DBGKD_ANY_WAIT_STATE_CHANGE *sc)
{
    CPUArchState *env = cpu->env_ptr;
    DBGKD_CONTROL_REPORT *cr = &sc->ControlReport;
    InitedAddr *KPCR = windbg_get_KPCR();
    target_ulong KPRCB;
    int err = 0;

    /* T0D0: HEADER */

    sc->Processor = 0;

    sc->NumberProcessors = 0;
    CPUState *cpu_tmp;
    CPU_FOREACH(cpu_tmp) {
        sc->NumberProcessors++;
    }
    stl_p(&sc->NumberProcessors, sc->NumberProcessors);

    KPRCB = READ_VMEM(cpu, KPCR->addr + OFFSET_KPRCB, target_ulong);
    sc->Thread = READ_VMEM(cpu, KPRCB + OFFSET_KPRCB_CURRTHREAD, target_ulong);
    sttul_p(&sc->Thread, sc->Thread);
    sttul_p(&sc->ProgramCounter, env->eip);

    /* T0D0: CONTROL REPORT */

    sttul_p(&cr->Dr6, env->dr[6]);
    sttul_p(&cr->Dr7, env->dr[7]);
    stw_p(&cr->ReportFlags, REPORT_INCLUDES_SEGS | REPORT_STANDARD_CS);
    stw_p(&cr->SegCs, env->segs[R_CS].selector);
    stw_p(&cr->SegDs, env->segs[R_DS].selector);
    stw_p(&cr->SegEs, env->segs[R_ES].selector);
    stw_p(&cr->SegFs, env->segs[R_FS].selector);
    stl_p(&cr->EFlags, env->eflags);

    err = cpu_memory_rw_debug(cpu, sc->ProgramCounter,
                              PTR(cr->InstructionStream[0]),
                              DBGKD_MAXSTREAM, 0);
    if (!err) {
        stw_p(&cr->InstructionCount, DBGKD_MAXSTREAM);
    }
}

SizedBuf kd_gen_exception_sc(CPUState *cpu)
{
    CPUArchState *env = cpu->env_ptr;
    DBGKD_ANY_WAIT_STATE_CHANGE *sc;
    DBGKM_EXCEPTION_RECORD64 *exc;
    SizedBuf buf;

    SBUF_MALLOC(buf, sizeof(DBGKD_ANY_WAIT_STATE_CHANGE) + sizeof(int));
    sc = (DBGKD_ANY_WAIT_STATE_CHANGE *) buf.data;
    exc = &sc->u.Exception.ExceptionRecord;
    kd_init_state_change(cpu, sc);

    stl_p(&sc->NewState, DbgKdExceptionStateChange);
    stl_p(&exc->ExceptionCode, 0x80000003);
    sttul_p(&exc->ExceptionAddress, env->eip);

    return buf;
}

SizedBuf kd_gen_load_symbols_sc(CPUState *cpu)
{
    DBGKD_ANY_WAIT_STATE_CHANGE *sc;
    SizedBuf buf;

    SBUF_MALLOC(buf, sizeof(DBGKD_ANY_WAIT_STATE_CHANGE));
    sc = (DBGKD_ANY_WAIT_STATE_CHANGE *) buf.data;
    kd_init_state_change(cpu, sc);

    stl_p(&sc->NewState, DbgKdLoadSymbolsStateChange);
    stl_p(&sc->u.LoadSymbols.PathNameLength, 0);

    return buf;
}
